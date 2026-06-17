#!/usr/bin/env python3
"""Single source of truth for the PGM ICS2115 MDFourier test signal.

Everything downstream is derived from here: the Z80 sequencer script bytes
(uploaded to the TestROM driver), the MDFourier ``.mfn`` profile, the static
voice template, and the expected capture length.  The signal is a standard
MDFourier layout — start sync pulse train, a dense rising tone sweep (one voice
played continuously, retuned per step via osc_fc), a silence (noise floor)
block, and an end sync pulse train.

Why this is reproducible across hardware and the simulator: the whole sequence
is driven by ICS timer 0 inside the Z80 IRQ handler, and the ICS timer and the
audio engine share the RTL clock, so every block/element boundary lands at the
same output-sample offset on both targets.  The two captures therefore differ
only by ICS audio fidelity — exactly what MDFourier measures.

Pitch model (from rtl/ics2115/ics2115_osc.sv): each output sample the
oscillator accumulator advances by ``osc_fc >> 1`` and the sample address is
``osc_acc >> 9``.  Looping a fixed ``LOOP_LEN``-byte region of the BIOS music
ROM therefore repeats at::

    f_loop = osc_fc * SAMPLE_RATE / (1024 * LOOP_LEN)

so pitch is proportional to osc_fc and one semitone is a factor of 2**(1/12).
A fixed loop with varying osc_fc keeps a constant timbre; the per-tick
sequencer rewrites osc_conf (0x00), osc_fc (0x01), pan (0x0c) and osc_ctl
(0x10).
"""

from __future__ import annotations

import dataclasses
import struct
from typing import List

# ── ICS clock / output rate ────────────────────────────────────────────────
ICS_CLK = 33_868_800           # ce_33m, the ICS reference clock (Hz)
OUTPUT_DIV = 1024              # (active_osc+1)*32 with active_osc = 31
SAMPLE_RATE = ICS_CLK / OUTPUT_DIV          # ~33074.21 Hz (true output rate)
SAMPLE_RATE_INT = round(SAMPLE_RATE)        # 33074, used in the WAV header
NYQUIST = SAMPLE_RATE / 2.0

# ── Timer 0 frame tick ──────────────────────────────────────────────────────
# period_clocks = ((scale[4:0]+1) * (preset+1)) << (4 + scale[7:5])    (RTL)
# scale0 = 0x7F -> mult 32, shift 3 ; preset0 = 0x89 (137) -> (137+1)=138
# period = (32 * 138) << 7 = 565248 ICS clocks = 552 output samples exactly.
TIMER_SCALE0 = 0x7F
TIMER_PRESET0 = 0x89
TIMER_PERIOD_CLOCKS = (((TIMER_SCALE0 & 0x1F) + 1) * (TIMER_PRESET0 + 1)) << (4 + (TIMER_SCALE0 >> 5))
SAMPLES_PER_FRAME = TIMER_PERIOD_CLOCKS / OUTPUT_DIV         # 522.0
FRAME_MS = TIMER_PERIOD_CLOCKS / ICS_CLK * 1000.0           # ~16.6893
FRAME_RATE = ICS_CLK / TIMER_PERIOD_CLOCKS                  # ~59.918 Hz

# Pre-roll: the test ROM holds silence for this many timer-0 ticks after
# MDF_START before entry 0 plays, so the capture is running before the start.
# MUST match MDF_START_DELAY_FRAMES in testroms/z80_ics_driver.c.  It is NOT in
# the profile (the analyzer aligns on the sync pulses, so leading silence is
# ignored) — but captures must run this much longer.
START_DELAY_FRAMES = 240                                    # ~4.0 s at FRAME_RATE
START_DELAY_SAMPLES = int(round(START_DELAY_FRAMES * SAMPLES_PER_FRAME))

# ── Looped BIOS sample region (validated music-ROM sample area) ──────────────
# A short sub-loop inside 0x2D640..0x3B2A0 so its repeat rate is the pitch.
LOOP_START = 0x2D640
LOOP_LEN = 64                  # bytes (8-bit linear samples)
LOOP_END = LOOP_START + LOOP_LEN

# ── Voice template (fixed parts; fc/pan/osc_ctl/osc_conf come from the script) ─
OSC_CONF = 0x08                # loop, linear-8, no IRQ (tone/sync/pan)
OSC_SADDR = 0x40
PAN_CENTER = 0x80

# ── Sequencer entry actions (mirror z80_ics_protocol.h) ─────────────────────
ACT_OFF = 0          # silence (osc stopped)
ACT_ON = 1           # key on at full volume — sharp edge (sync pulses)
ACT_ON_RAMP = 2      # key on with a short volume ramp — de-popped (tone start)
ACT_OFF_RAMP = 3     # key off with a volume ramp DOWN (legacy, unused)
ACT_FC = 4           # change osc_fc only, voice keeps playing — the tone sweep
                     # changes pitch with no key-on/off, so no host/sequencer
                     # write races (just one osc_fc write per step)

# Volume-envelope attack ramp (applied at key-on for ACT_ON_RAMP).  The voice
# template sets the rate (vol_incr) / mode (vmode) / window (vol_start..vol_end);
# the sequencer just starts vol_acc at 0.  vmode=2 is the LINEAR rate law
# (step = vol_incr << 10 per sample; modes 0/1/3 are exponential — see
# rtl/ics2115/ics2115_osc.sv calc_vol_step).  Linear, vol_incr=0xff ramps
# vol_acc 0 -> full (vol_end<<18) in (0xff<<18)/(0xff<<10) = 256 output samples
# (~7.7 ms) — well under one element and skipped by the block cutFrames, so it
# kills the click without coloring the analyzed steady state.
RAMP_INCR = 0xFF
RAMP_VMODE = 0x02              # 2 = linear envelope

MDF_MAX_ENTRIES = 256          # must match Z80_ICS_MDF_MAX_ENTRIES

# ── Sync pulse train ────────────────────────────────────────────────────────
# MDFourier detects pulseCount tone pulses (each pulseFrameLen frames) with
# silence gaps of the same length.  8820 Hz is the canonical sync frequency.
SYNC_FREQ = 8820
SYNC_PULSE_FRAMES = 1         # pulseFrameLen
SYNC_PULSE_COUNT = 10          # pulseCount

# ── Tone sweep ──────────────────────────────────────────────────────────────
# One voice plays continuously; only osc_fc changes per step (see _tone_block).
TONE_BASE_FREQ = 110.0         # A2
TONE_STEPS_PER_OCTAVE = 24     # quarter-tone granularity (was 12 = semitones)
TONE_OCTAVES = 6               # 110 Hz -> ~7040 Hz
TONE_COUNT = TONE_OCTAVES * TONE_STEPS_PER_OCTAVE + 1   # 145 tones
TONE_FRAMES = 16               # frames held per tone (~267 ms)

# ── Silence (noise floor) ───────────────────────────────────────────────────
SILENCE_FRAMES = 48


def configure(short: bool = False) -> None:
    """Adjust the signal size.  ``short`` produces a much smaller signal for
    fast simulator smoke tests; the profile and capture length stay consistent
    because everything is derived from these module globals.
    """
    global TONE_COUNT, TONE_FRAMES, SYNC_PULSE_COUNT, SILENCE_FRAMES
    if short:
        TONE_COUNT = 25
        TONE_FRAMES = 16
        SYNC_PULSE_COUNT = 6
        SILENCE_FRAMES = 24
    else:
        TONE_COUNT = TONE_OCTAVES * TONE_STEPS_PER_OCTAVE + 1
        TONE_FRAMES = 16
        SYNC_PULSE_COUNT = 10
        SILENCE_FRAMES = 48


def fc_for_freq(freq: float) -> int:
    """osc_fc that makes the LOOP_LEN-byte loop repeat at ``freq`` Hz.

    Rounded to an even value because the RTL uses osc_fc[15:1] (the LSB is
    ignored), and clamped to the valid 16-bit oscillator range.
    """
    fc = round(freq * OUTPUT_DIV * LOOP_LEN / SAMPLE_RATE)
    fc &= ~1
    if fc < 2:
        fc = 2
    if fc > 0xFFFE:
        fc = 0xFFFE
    return fc


def tone_freqs() -> List[float]:
    return [TONE_BASE_FREQ * (2.0 ** (n / TONE_STEPS_PER_OCTAVE))
            for n in range(TONE_COUNT)]


@dataclasses.dataclass
class Entry:
    fc: int
    pan: int
    action: int
    ticks: int
    osc_conf: int = OSC_CONF        # voice format (per-entry override; blocks use default)

    def pack(self) -> bytes:
        # 8 bytes: fc(u16) pan osc_conf action reserved ticks(u16)
        return struct.pack(">HBBBBH", self.fc & 0xFFFF, self.pan & 0xFF,
                           self.osc_conf & 0xFF, self.action & 0xFF,
                           0, self.ticks & 0xFFFF)


@dataclasses.dataclass
class Block:
    """One MDFourier profile block plus the script entries that emit it."""
    name: str
    type_field: str        # 's','n','k' or a numeric type id as a string
    element_count: int
    frames: int            # frames per element (after MDFourier flattening)
    cut_frames: int
    color: str
    channel: str           # 'm', 'S', 'n'
    entries: List[Entry]

    def profile_line(self) -> str:
        return f"{self.name} {self.type_field} {self.element_count} {self.frames} {self.cut_frames} {self.color} {self.channel}"


def _sync_block(name: str, cut: int) -> Block:
    fc = fc_for_freq(SYNC_FREQ)
    entries: List[Entry] = []
    for _ in range(SYNC_PULSE_COUNT):
        entries.append(Entry(fc, PAN_CENTER, ACT_ON, SYNC_PULSE_FRAMES))
        entries.append(Entry(fc, PAN_CENTER, ACT_OFF, SYNC_PULSE_FRAMES))
    total = SYNC_PULSE_COUNT * SYNC_PULSE_FRAMES * 2
    return Block(name, "s", 1, total, cut, "red", "m", entries)


def _silence_block(name: str, cut: int) -> Block:
    return Block(name, "n", 1, SILENCE_FRAMES, cut, "gray", "m",
                 [Entry(0, PAN_CENTER, ACT_OFF, SILENCE_FRAMES)])


def _tone_block() -> Block:
    # One continuously-playing voice: key on (with a short ramp to de-pop the
    # start) on the first tone, then only change osc_fc per step.  No per-tone
    # key-on/off, no gaps — changing osc_fc retunes the looping voice smoothly,
    # so there are no host-write/sequencer collisions during the sweep.
    freqs = tone_freqs()
    entries = [Entry(fc_for_freq(freqs[0]), PAN_CENTER, ACT_ON_RAMP, TONE_FRAMES)]
    for f in freqs[1:]:
        entries.append(Entry(fc_for_freq(f), PAN_CENTER, ACT_FC, TONE_FRAMES))
    # cut skips the start of each element (attack ramp on tone 0; the osc_fc
    # retune transient on the rest) so only the steady pitch is analyzed.
    return Block("Tones", "1", TONE_COUNT, TONE_FRAMES, 2, "green", "m", entries)


def build_blocks() -> List[Block]:
    """The ordered block list — drives both the script and the profile."""
    return [
        _sync_block("Sync", 0),
        _silence_block("Silence", 0),
        _tone_block(),
        _silence_block("Silence", 0),
        _sync_block("Sync", 0),
    ]


def build_script(blocks=None) -> List[Entry]:
    if blocks is None:
        blocks = build_blocks()
    script: List[Entry] = []
    for b in blocks:
        script.extend(b.entries)
    if len(script) > MDF_MAX_ENTRIES:
        raise ValueError(f"script has {len(script)} entries, max is {MDF_MAX_ENTRIES}")
    return script


def pack_script(script) -> bytes:
    return b"".join(e.pack() for e in script)


def total_frames(blocks=None) -> int:
    if blocks is None:
        blocks = build_blocks()
    return sum(e.ticks for b in blocks for e in b.entries)


def total_samples(blocks=None) -> int:
    return int(round(total_frames(blocks) * SAMPLES_PER_FRAME))


def voice_template() -> dict:
    """Fixed voice-0 fields (loop region + full static volume).

    fc / pan / osc_ctl are overwritten by the sequencer per script entry, so
    only the loop, volume window and format need to be set here.
    """
    return {
        "osc_conf": OSC_CONF,
        "osc_fc": fc_for_freq(SYNC_FREQ),
        "start_wave": LOOP_START,
        "end_wave": LOOP_END,
        "acc_wave": LOOP_START,
        "osc_saddr": OSC_SADDR,
        "vol_acc": 0xFFFF,      # full (first sync keys on instantly)
        "vol_start": 0x00,      # ramp low bound (silence)
        "vol_end": 0xFF,        # ramp high bound (full)
        "vol_incr": RAMP_INCR,  # attack ramp rate (sequencer keys ramp from 0)
        "vol_ctrl": 0x00,
        "pan": PAN_CENTER,
        "osc_ctl": 0x0F,        # start stopped; entry 0 keys it on
        "vmode": RAMP_VMODE,
    }


def describe() -> str:
    blocks = build_blocks()
    script = build_script(blocks)
    lines = [
        f"sample rate     : {SAMPLE_RATE:.3f} Hz (header {SAMPLE_RATE_INT})",
        f"frame tick      : {FRAME_RATE:.3f} Hz, {FRAME_MS:.4f} ms, {SAMPLES_PER_FRAME:.1f} samples",
        f"timer 0         : scale=0x{TIMER_SCALE0:02x} preset=0x{TIMER_PRESET0:02x} period={TIMER_PERIOD_CLOCKS} clk",
        f"loop region     : 0x{LOOP_START:05x}..0x{LOOP_END:05x} ({LOOP_LEN} bytes)",
        f"sync            : {SYNC_FREQ} Hz, {SYNC_PULSE_COUNT} pulses x {SYNC_PULSE_FRAMES} frames, fc={fc_for_freq(SYNC_FREQ)}",
        f"tones           : {TONE_COUNT} steps ({TONE_STEPS_PER_OCTAVE}/octave, fc-only sweep) "
        f"{tone_freqs()[0]:.1f}..{tone_freqs()[-1]:.1f} Hz, "
        f"fc {fc_for_freq(tone_freqs()[0])}..{fc_for_freq(tone_freqs()[-1])}, {TONE_FRAMES} frames each",
        f"script entries  : {len(script)} / {MDF_MAX_ENTRIES}",
        f"total           : {total_frames(blocks)} frames, {total_samples(blocks)} samples, "
        f"{total_samples(blocks)/SAMPLE_RATE:.2f} s",
        "",
        "blocks:",
    ]
    for b in blocks:
        lines.append(f"  {b.profile_line()}")
    return "\n".join(lines)


if __name__ == "__main__":
    print(describe())
