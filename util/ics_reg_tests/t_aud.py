"""T-AUD: audio-observable register behavior via onset-anchored captures.

Matrix rows: 00-B (format bits), 0C-A/09-A (pan/vol regression), 12-C (VMode
ramp families), 10-B (OscCtl key-on variants).

Comparison strategy (see docs/ics2115_register_matrix.md): every test is
silence-bracketed and reduced to phase-immune derived metrics — steady-window
RMS per channel, zero-crossing rate (pitch proxy), peak, and ramp durations
measured from the audible rise inside a ring-buffer window.  Raw PCM is never
diffed: capture-start jitter and per-target key-on phase make that
meaningless; the derived numbers carry relative tolerances in the differ.
"""

from __future__ import annotations

from util.ics2115_remote import WIDTH_16, WIDTH_LOWER8, WIDTH_UPPER8, Voice

from .harness import Emit, quiesce

TEST_VOICE = 6
STEADY_SAMPLES = 8192          # ~0.25 s steady window
NOISE_FLOOR = 8


def _loop_tone_voice(fmt: int = 2, pan: int = 0x80, vol_acc: int = 0xFFFF) -> Voice:
    """The known looping sample from the BIOS music ROM (SAMPLE1 in
    z80_ics_make_sample_voice): a steady tone suitable for RMS/ZCR metrics."""
    v = Voice()
    v.osc_conf = 0x08 | (fmt & 0x03)   # loop + sample format bits
    v.osc_fc = 12081
    v.vol_acc = vol_acc
    v.vol_start = 0xFF
    v.vol_end = 0xFF
    v.vol_ctrl = 0x00
    v.vol_incr = 0x00
    v.pan = pan
    v.vmode = 0x00
    v.set_start_wave_addr(0x2D640)
    v.set_end_wave_addr(0x3B2A0)
    v.set_acc_wave_addr(0x2D640)
    v.osc_ctl = 0x0F                   # keyed off; key-on per test
    return v


def _metrics(samples: list[tuple[int, int]]) -> dict:
    L = [s[0] for s in samples]
    R = [s[1] for s in samples]
    def rms(xs):
        return round((sum(x * x for x in xs) / max(len(xs), 1)) ** 0.5, 1)
    zc = sum(1 for a, b in zip(L, L[1:]) if (a < 0) != (b < 0))
    return {
        "rms_l": rms(L),
        "rms_r": rms(R),
        "peak": max((max(L), -min(L), max(R), -min(R))) if L else 0,
        "zcr": round(zc / max(len(L), 1), 4),
    }


def _settle(ics, frames: int = 3) -> None:
    """Advance both targets uniformly (each poll costs >= 1 frame on sim,
    real time on hardware)."""
    f0, _ = ics.get_irq_counts_timed()
    while True:
        f1, _ = ics.get_irq_counts_timed()
        if f1 - f0 >= frames:
            return


def _capture(ics, n: int) -> list[tuple[int, int]]:
    """Contiguous n-sample window on either target: the hardware extractor
    arms at most 128 samples per ARM_FRAMES, so use its block mode (128
    samples per DMA block); the sim reader takes the frame count directly."""
    capture_blocks = getattr(ics.audio, "capture_blocks", None)
    if capture_blocks is not None:
        blocks = capture_blocks((n + 127) // 128, timeout=60.0)
        out: list[tuple[int, int]] = []
        for b in blocks:
            out.extend(b.samples)
        return out[:n]
    return ics.capture_audio_frames(n, timeout=60.0)


def _steady(ics) -> list[tuple[int, int]]:
    return _capture(ics, STEADY_SAMPLES)


def _keyon(ics, voice: Voice) -> None:
    v = Voice(**{f: getattr(voice, f) for f in voice.__dataclass_fields__})
    v.osc_ctl = 0x00
    ics.write_voice(TEST_VOICE, v)


def _keyoff(ics) -> None:
    ics.write_reg(TEST_VOICE, 0x10, 0x0F, width=WIDTH_UPPER8)
    ics.write_reg(TEST_VOICE, 0x00, 0x00, width=WIDTH_UPPER8)


def _silence_floor(ics, emit: Emit) -> None:
    m = _metrics(_steady(ics))
    emit("t_aud.silence_floor", {}, m)


def _tone_formats(ics, emit: Emit) -> None:
    # OscConf[1:0] firmware encoding (TB doc): 00 lin8, 01 ulaw8, 10 lin16,
    # 11 noise/special — same ROM data, four decode modes.
    for fmt in (2, 0, 1, 3):
        _keyon(ics, _loop_tone_voice(fmt=fmt))
        _settle(ics)
        m = _metrics(_steady(ics))
        _keyoff(ics)
        emit("t_aud.tone_fmt", {"fmt": fmt}, m)


def _pan_levels(ics, emit: Emit) -> None:
    for pan in (0x00, 0x40, 0x80, 0xC0, 0xF0):
        _keyon(ics, _loop_tone_voice(pan=pan))
        _settle(ics)
        m = _metrics(_steady(ics))
        _keyoff(ics)
        emit("t_aud.pan_levels", {"pan": pan}, m)


def _vol_levels(ics, emit: Emit) -> None:
    for vol in (0x4000, 0x8000, 0xC000, 0xFFFF):
        _keyon(ics, _loop_tone_voice(vol_acc=vol))
        _settle(ics)
        m = _metrics(_steady(ics))
        _keyoff(ics)
        emit("t_aud.vol_levels", {"vol_acc": vol}, m)


def _vmode_ramps(ics, emit: Emit) -> None:
    # Ramp-up duration per VMode rate family at fixed VIncr: key the tone at
    # vol_acc=0 (audible source, zero volume), then launch the ramp and find
    # the rise inside a ring window — the audible rise is the time anchor.
    for vmode in (0, 1, 2, 3):
        v = _loop_tone_voice(vol_acc=0x0000)
        v.vol_start = 0x00
        v.vol_end = 0xFF
        v.vmode = vmode
        _keyon(ics, v)
        _settle(ics)
        # launch the ramp, then clear-capture: the capture starts within
        # ~one command of the launch, so the rise lands inside the window
        # (slow families show leading silence — that is the onset anchor).
        ics.write_reg(TEST_VOICE, 0x12, vmode, width=WIDTH_UPPER8)
        ics.write_reg(TEST_VOICE, 0x06, 0x04, width=WIDTH_UPPER8)
        ics.write_reg(TEST_VOICE, 0x0D, 0x00, width=WIDTH_UPPER8)
        window = _capture(ics, 49152)
        _keyoff(ics)
        ics.write_reg(TEST_VOICE, 0x06, 0x00, width=WIDTH_UPPER8)
        ics.write_reg(TEST_VOICE, 0x0D, 0x03, width=WIDTH_UPPER8)
        L = [abs(s[0]) for s in window]
        if not L:
            emit("t_aud.vmode_ramp", {"vmode": vmode}, {"error": "no samples"})
            continue
        peak = max(L)
        if peak < NOISE_FLOOR * 4:
            emit("t_aud.vmode_ramp", {"vmode": vmode}, {"ramp_samples": -1, "peak": peak})
            continue
        # 64-sample envelope maxima for the rise measurement
        env = [max(L[i:i + 64]) for i in range(0, len(L), 64)]
        hi = max(env)
        t10 = next(i for i, e in enumerate(env) if e > hi * 0.1)
        t90 = next(i for i, e in enumerate(env) if e > hi * 0.9)
        emit("t_aud.vmode_ramp", {"vmode": vmode},
             {"ramp_samples": (t90 - t10) * 64, "peak": peak})


def _octl_variants(ics, emit: Emit) -> None:
    # Key-on with 0x00 vs the Turtle Beach filterconfig variants: do the
    # modes differ audibly? (matrix 10-B / T-OCTL overlap)
    for ctl in (0x00, 0x10, 0x20, 0x30):
        v = _loop_tone_voice()
        v.osc_ctl = ctl
        ics.write_voice(TEST_VOICE, v)
        _settle(ics)
        m = _metrics(_steady(ics))
        _keyoff(ics)
        emit("t_aud.octl_variants", {"ctl": ctl}, m)


def _sys_gate_audio(ics, emit: Emit) -> None:
    # Does 0x4D bits 0&2 gate ALL voice behavior (audio + oscillator advance)
    # or only the IRQ output?  Play a tone, clear the bits, measure both
    # output RMS and whether OscAcc (0x0A) keeps advancing.
    ics.write_reg(0, 0x4D, 0x0D, width=WIDTH_LOWER8)
    _keyon(ics, _loop_tone_voice())
    _settle(ics)
    for label, sysd in (("gate_on_0x0d", 0x0D), ("clear_0x04", 0x04),
                        ("clear_0x00", 0x00), ("restore_0x05", 0x05)):
        ics.write_reg(0, 0x4D, sysd, width=WIDTH_LOWER8)
        _settle(ics)
        acc0 = ics.read_reg(TEST_VOICE, 0x0A, width=WIDTH_16)
        m = _metrics(_steady(ics))
        acc1 = ics.read_reg(TEST_VOICE, 0x0A, width=WIDTH_16)
        emit("t_aud.sys_gate", {"sys_ctl": sysd, "label": label},
             {"rms_l": m["rms_l"], "rms_r": m["rms_r"], "peak": m["peak"],
              "osc_advanced": acc0 != acc1})
    _keyoff(ics)
    ics.write_reg(0, 0x4D, 0x0D, width=WIDTH_LOWER8)


def run(ics, emit: Emit) -> None:
    quiesce(ics)
    ics.open_audio()
    _silence_floor(ics, emit)
    for sub in (_tone_formats, _pan_levels, _vol_levels, _vmode_ramps,
                _octl_variants, _sys_gate_audio):
        sub(ics, emit)
        quiesce(ics)  # full re-baseline between subtests: no state leakage
