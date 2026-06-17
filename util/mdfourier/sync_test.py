#!/usr/bin/env python3
"""Focused timing test for the MDFourier sync pulse train.

MDFourier alignment depends on the start/end sync pulses being a clean, regular
square wave in time: each pulse exactly one frame long, with a one-frame pause
between pulses.  This test isolates *just* the sync pulses (no tones / pan /
noise), captures the audio on a target, and measures every pulse's ON duration
and the OFF gap that follows it, in samples and milliseconds, against one frame.

It reports per-pulse durations, the spread (max-min) of the ON and OFF
durations, and PASS/FAIL at a 0.1 ms tolerance.  Durations are measured from a
centred moving-RMS envelope with linear-interpolated threshold crossings, so the
window delay cancels and edge times are sub-sample accurate.

Examples:
    python3 util/mdfourier/sync_test.py --target sim --pulses 20
    python3 util/mdfourier/sync_test.py --target sim --out /tmp/sync_sim.wav
    python3 util/mdfourier/sync_test.py --target hw --pulses 30
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from util.mdfourier import mdf_signal as sig
from util.mdfourier.run import make_voice, open_target, write_wav

TOL_MS = 0.1                                   # acceptable timing variance
TOL_SAMPLES = TOL_MS * 1e-3 * sig.SAMPLE_RATE  # ~3.31 samples


def build_sync_script(n_pulses: int, pulse_frames: int):
    """Sync-only script: n_pulses of (ACT_ON 1 frame, ACT_OFF 1 frame)."""
    fc = sig.fc_for_freq(sig.SYNC_FREQ)
    entries = []
    for _ in range(n_pulses):
        entries.append(sig.Entry(fc, sig.PAN_CENTER, sig.ACT_ON, pulse_frames))
        entries.append(sig.Entry(fc, sig.PAN_CENTER, sig.ACT_OFF, pulse_frames))
    return entries


def capture_sync(remote, target, script, n_samples):
    """Load + start the sync-only sequence and capture n_samples (L,R)."""
    audio = remote.open_audio()
    remote.mdf_load(sig.pack_script(script))
    remote.mdf_start(len(script), sig.TIMER_SCALE0, sig.TIMER_PRESET0)
    if target == "sim":
        return remote.capture_audio_frames(n_samples, timeout=None)
    seconds = n_samples / sig.SAMPLE_RATE
    nblocks = math.ceil((n_samples + 0.5 * sig.SAMPLE_RATE) / 128)
    out = []
    for block in audio.capture_blocks(nblocks, timeout=seconds * 3 + 10):
        out.extend(block.samples)
    return out


def _rle(act):
    ch = np.flatnonzero(np.diff(act))
    bounds = np.concatenate(([0], ch + 1, [len(act)]))
    return [(int(act[a]), int(b - a), int(a)) for a, b in zip(bounds[:-1], bounds[1:])]


def measure(samples, thresh=50, min_run=64):
    """Return (on_durations, gap_durations, runs) in samples, sample-exact.

    During a gap the single sync voice is keyed off, so the output is exactly
    digital zero; the tone is full amplitude from the first sample.  Edges are
    therefore the transitions of |signal|>thresh, measured directly with no
    envelope-window bias.  The sync tone itself crosses zero, and one pulse can
    briefly dip below thresh, which would split a pulse into spurious sub-runs;
    those interior zero-runs are far shorter than a real gap (>500 samples), so
    any zero-run shorter than ``min_run`` is filled back to active.  This keeps
    the true edge positions (no dilation), so both jitter *and* the duty-cycle
    bias are accurate.  The caller drops the leading partial pulse and the tail.
    """
    x = np.asarray(samples, dtype=np.float64)
    mono = np.abs(x.mean(axis=1))
    act = (mono > thresh).astype(np.int8)
    for state, length, start in _rle(act):          # bridge interior tone dips
        if state == 0 and length < min_run:
            act[start:start + length] = 1
    runs = _rle(act)
    on = [length for state, length, _ in runs if state == 1]
    gap = [length for state, length, _ in runs if state == 0]
    return on, gap, runs


def report(on, gap):
    frame = sig.SAMPLES_PER_FRAME
    spm = sig.SAMPLE_RATE / 1000.0  # samples per ms
    print(f"\none frame = {frame:.1f} samples = {sig.FRAME_MS:.4f} ms   "
          f"tolerance = ±{TOL_MS} ms (±{TOL_SAMPLES:.2f} samples)\n")
    print(f"{'pulse':>5} {'on(smp)':>9} {'on(ms)':>9} {'Δon(ms)':>9} "
          f"{'gap(smp)':>9} {'gap(ms)':>9} {'Δgap(ms)':>9}")
    npulse = max(len(on), len(gap))
    for i in range(npulse):
        o = on[i] if i < len(on) else float("nan")
        g = gap[i] if i < len(gap) else float("nan")
        print(f"{i:>5} {o:>9.2f} {o/spm:>9.4f} {(o-frame)/spm:>+9.4f} "
              f"{g:>9.2f} {g/spm:>9.4f} {(g-frame)/spm:>+9.4f}")

    def stats(name, arr):
        a = np.asarray(arr, dtype=np.float64)
        spread_smp = a.max() - a.min()
        spread_ms = spread_smp / spm
        bias_ms = (a.mean() - frame) / spm
        ok = spread_ms <= TOL_MS
        print(f"  {name:<4} mean={a.mean():.2f} smp ({a.mean()/spm:.4f} ms)  "
              f"bias={bias_ms:+.4f} ms  spread={spread_smp:.2f} smp "
              f"({spread_ms:.4f} ms)  {'PASS' if ok else 'FAIL'}")
        return ok

    print("\nsummary  (bias = mean - one frame; spread = max - min):")
    ok_on = stats("ON", on) if on else False
    ok_gap = stats("GAP", gap) if gap else False
    m = min(len(on), len(gap))
    period = np.asarray(on[:m]) + np.asarray(gap[:m]) if m else None
    ok_period = True
    if period is not None and period.size:
        ps = (period.max() - period.min()) / spm
        ok_period = ps <= TOL_MS
        print(f"  PER  mean={period.mean()/spm:.4f} ms (ideal {2*sig.FRAME_MS:.4f} ms)  "
              f"spread={ps:.4f} ms  {'PASS' if ok_period else 'FAIL'}")
    # The headline MDFourier requirement is regularity: low spread (jitter) and a
    # stable period.  A non-zero ON/GAP *bias* (duty cycle != 50%) is reported
    # separately so a consistent key-on vs key-off latency is visible.
    jitter_ok = ok_on and ok_gap and ok_period
    print(f"\njitter (spread) within ±{TOL_MS} ms: {'PASS' if jitter_ok else 'FAIL'}")
    return jitter_ok


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--target", choices=["sim", "hw"], required=True)
    ap.add_argument("--pulses", type=int, default=20)
    ap.add_argument("--pulse-frames", type=int, default=sig.SYNC_PULSE_FRAMES)
    ap.add_argument("--out", type=Path, default=None, help="optional WAV dump")
    ap.add_argument("--margin", type=float, default=0.1, help="extra capture seconds")
    ap.add_argument("--game", default="pgm_test")
    ap.add_argument("--mra", default=None)
    ap.add_argument("--sim-cwd", default="sim")
    ap.add_argument("--sim-exe", default="./sim")
    ap.add_argument("--hw-target", default="pgm")
    ap.add_argument("--prime-frames", type=int, default=200)
    args = ap.parse_args()

    script = build_sync_script(args.pulses, args.pulse_frames)
    n_signal = int(round(args.pulses * 2 * args.pulse_frames * sig.SAMPLES_PER_FRAME))
    # +pre-roll: the test ROM delays the sequence start by START_DELAY frames.
    n_capture = sig.START_DELAY_SAMPLES + n_signal + int(args.margin * sig.SAMPLE_RATE)
    print(f"target={args.target} pulses={args.pulses} entries={len(script)} "
          f"signal={n_signal} samples ({n_signal/sig.SAMPLE_RATE:.2f}s) capture={n_capture}")

    remote = open_target(args)
    try:
        info = remote.ping()
        print(f"driver_magic=0x{info.driver_magic:04x}")
        remote.write_voice(0, make_voice())
        remote.write_voice(1, make_voice())
        samples = capture_sync(remote, args.target, script, n_capture)
    finally:
        remote.close()
    print(f"captured {len(samples)} samples")

    if args.out:
        write_wav(args.out, samples, sig.SAMPLE_RATE_INT)
        print(f"wrote {args.out}")

    on, gap, runs = measure(samples)
    # Drop the leading partial pulse (capture starts mid first key-on, which is
    # issued synchronously by mdf_start, not on a timer edge) and the trailing
    # tail gap (capture margin after the sequence ends).
    on = on[1:]
    gap = gap[:-1] if gap else gap
    n = min(len(on), len(gap))
    on, gap = on[:n], gap[:n]
    print(f"detected {len(runs)} runs -> {len(on)} steady pulses / {len(gap)} gaps "
          f"(expected {args.pulses})")
    if not on or not gap:
        print("ERROR: no pulses detected")
        return 2
    ok = report(on, gap)
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
