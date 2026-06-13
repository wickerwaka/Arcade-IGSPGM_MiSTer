"""T-RATE: output sample rate vs ActiveOsc (0x0E), measured by tone pitch.

Matrix rows: 0E-A (sample_rate = clock / ((active_osc+1) * 32)), 0E-B.

The active_osc divider sets the ICS *internal* sample/playback rate.  Measuring
the captured output *stream* rate is blind to it (the output is resampled to a
fixed cadence).  The divider's real, content-intrinsic effect is on PLAYBACK
PITCH: a fixed-OscFC tone plays faster as active_osc drops, surviving resample
as a higher frequency.  So we measure the tone's zero-crossing rate (pitch
proxy) across active_osc; the ratio vs active_osc=31 tracks 32/(n+1) until the
pitch nears Nyquist and aliases (low n excluded).
"""

from __future__ import annotations

from util.ics2115_remote import WIDTH_UPPER8, Voice

from .harness import Emit, quiesce

TONE_VOICE = 0
CAP_SAMPLES = 8192
# Skip 0/1 (pitch would alias past Nyquist: 32x/16x the base tone).
ACTIVE_VALUES = [31, 23, 15, 11, 7]


def _tone(ics) -> None:
    v = Voice()
    v.osc_conf = 0x08
    v.osc_fc = 4096            # lowish base pitch so higher rates stay sub-Nyquist
    v.vol_acc = 0xFFFF
    v.vol_start = 0xFF
    v.vol_end = 0xFF
    v.pan = 0x80
    v.set_start_wave_addr(0x2D640)
    v.set_end_wave_addr(0x3B2A0)
    v.set_acc_wave_addr(0x2D640)
    v.osc_ctl = 0x00
    ics.write_voice(TONE_VOICE, v)


def _capture(ics, n: int):
    cb = getattr(ics.audio, "capture_blocks", None)
    if cb is not None:
        out = []
        for b in cb((n + 127) // 128, timeout=60.0):
            out.extend(b.samples)
        return out[:n]
    return ics.capture_audio_frames(n, timeout=60.0)


def _zcr(samples) -> float:
    L = [s[0] for s in samples]
    if not L:
        return -1.0
    z = sum(1 for a, b in zip(L, L[1:]) if (a < 0) != (b < 0))
    return round(z / len(L), 4)


def run(ics, emit: Emit) -> None:
    quiesce(ics)
    ics.open_audio()
    base = None
    for n in ACTIVE_VALUES:
        ics.write_reg(0, 0x0E, n, width=WIDTH_UPPER8)
        _tone(ics)
        # settle a few captures so the rate change takes effect
        _capture(ics, 1024)
        zcr = _zcr(_capture(ics, CAP_SAMPLES))
        ics.write_reg(TONE_VOICE, 0x10, 0x0F, width=WIDTH_UPPER8)
        if n == 31:
            base = zcr
        ratio = round(zcr / base, 3) if base else -1
        emit("t_rate.tone_pitch", {"active_osc": n},
             {"zcr": zcr, "zcr_ratio_vs31": ratio,
              "expected_ratio": round(32 / (n + 1), 3)})
    ics.write_reg(0, 0x0E, 0x1F, width=WIDTH_UPPER8)
    quiesce(ics)
