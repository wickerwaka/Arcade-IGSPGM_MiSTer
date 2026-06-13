#!/usr/bin/env python3
"""Compare two MDFourier captures (hardware vs simulator) of the PGM signal.

Because the signal is timer-locked, the two captures are already sample-aligned
by construction; this tool verifies that, then does a per-element spectral
comparison (peak frequency + magnitude) so any ICS2115 fidelity gap shows up
localized to a tone/pan element.  It also prints the exact MDFourier command
line (and runs it if --mdfourier is given) for the full Fourier analysis.

    python3 util/mdfourier/compare.py --ref /tmp/mdf_sim.wav --comp /tmp/mdf_hw.wav
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from util.mdfourier import mdf_signal as sig

DEFAULT_PROFILE = Path(__file__).resolve().parent / "pgm_mdfourier.mfn"


def load_wav(path: Path):
    import numpy as np

    with wave.open(str(path), "rb") as w:
        rate = w.getframerate()
        ch = w.getnchannels()
        n = w.getnframes()
        raw = w.readframes(n)
    data = np.frombuffer(raw, dtype="<i2").astype(np.float64)
    if ch == 2:
        data = data.reshape(-1, 2)
    else:
        data = np.stack([data, data], axis=1)
    return data, rate


def cross_lag(a, b, search=4096):
    """Integer sample lag that best aligns mono(b) to mono(a)."""
    import numpy as np

    am = a.mean(axis=1)
    bm = b.mean(axis=1)
    n = min(len(am), len(bm), 1 << 16)
    am = am[:n] - am[:n].mean()
    bm = bm[:n] - bm[:n].mean()
    best_lag, best = 0, -1e30
    for lag in range(-search, search + 1, 1):
        if lag >= 0:
            x, y = am[lag:], bm[: len(am) - lag]
        else:
            x, y = am[: len(am) + lag], bm[-lag:]
        m = min(len(x), len(y))
        if m < 1000:
            continue
        c = float((x[:m] * y[:m]).sum())
        if c > best:
            best, best_lag = c, lag
    return best_lag


def element_windows():
    """Yield (label, expected_freq_or_None, start_sample, end_sample) per ON entry."""
    blocks = sig.build_blocks()
    frame = 0
    spf = sig.SAMPLES_PER_FRAME
    for b in blocks:
        for i, e in enumerate(b.entries):
            start = int(round(frame * spf))
            frame += e.ticks
            end = int(round(frame * spf))
            if e.action != sig.ACT_ON:
                continue
            if b.name == "Tones":
                freq = e.fc * sig.SAMPLE_RATE / (1024 * sig.LOOP_LEN)
                label = f"{b.name}[{i}]"
            elif b.name == "PanSweep":
                freq = e.fc * sig.SAMPLE_RATE / (1024 * sig.LOOP_LEN)
                label = f"{b.name} pan=0x{e.pan:02x}"
            else:
                freq = sig.SYNC_FREQ
                label = b.name
            yield label, freq, start, end


def peak(window):
    import numpy as np

    if len(window) < 64:
        return 0.0, -120.0
    w = window * np.hanning(len(window))
    spec = np.abs(np.fft.rfft(w))
    freqs = np.fft.rfftfreq(len(window), 1.0 / sig.SAMPLE_RATE)
    k = int(np.argmax(spec))
    mag = spec[k] / (len(window) / 2)
    db = 20.0 * np.log10(mag + 1e-9)
    return float(freqs[k]), float(db)


def local_lag(ref_seg, comp, guess, search=2200, step=16):
    """Refine the lag for one element by local correlation.

    The hardware extractor's sample clock drifts slightly from the nominal
    33075 Hz, so a single global lag is wrong by the end of the signal; aligning
    each element locally tracks the drift (same idea as MDFourier's per-sync
    alignment).
    """
    import numpy as np

    n = len(ref_seg)
    rseg = ref_seg - ref_seg.mean()
    best_d, best = 0, -1e30
    for d in range(-search, search + 1, step):
        a = guess + d
        if a < 0 or a + n > len(comp):
            continue
        cseg = comp[a:a + n]
        c = float((rseg * (cseg - cseg.mean())).sum())
        if c > best:
            best, best_d = c, d
    return best_d


def analyze(ref, comp, lag, cut_frames=4):
    import numpy as np

    rows = []
    cut = int(cut_frames * sig.SAMPLES_PER_FRAME)
    for label, exp_freq, s, e in element_windows():
        rs, re = s + cut, e
        if re > len(ref):
            continue
        ref_seg = ref[rs:re].mean(axis=1)
        d = local_lag(ref_seg, comp.mean(axis=1), rs + lag)
        cs, ce = rs + lag + d, re + lag + d
        if ce > len(comp) or cs < 0:
            continue
        rf, rdb = peak(ref_seg)
        cf, cdb = peak(comp[cs:ce].mean(axis=1))
        rows.append((label, exp_freq, rf, rdb, cf, cdb, cdb - rdb))
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ref", type=Path, required=True, help="reference WAV (e.g. sim)")
    ap.add_argument("--comp", type=Path, required=True, help="comparison WAV (e.g. hw)")
    ap.add_argument("--profile", type=Path, default=DEFAULT_PROFILE)
    ap.add_argument("--short", action="store_true", help="captures were made with --short")
    ap.add_argument("--mdfourier", type=Path, default=None, help="path to mdfourier binary to run")
    args = ap.parse_args()

    sig.configure(short=args.short)
    import numpy as np  # noqa: F401  (import early so a missing dep fails clearly)

    ref, ref_rate = load_wav(args.ref)
    comp, comp_rate = load_wav(args.comp)
    print(f"ref  {args.ref}  {len(ref)} frames @ {ref_rate} Hz")
    print(f"comp {args.comp}  {len(comp)} frames @ {comp_rate} Hz")
    if ref_rate != comp_rate:
        print("WARNING: sample rates differ; comparison assumes matching rates")

    lag = cross_lag(ref, comp)
    print(f"alignment lag (comp vs ref): {lag} samples "
          f"({lag / sig.SAMPLE_RATE * 1000:.2f} ms) — expect ~0 for timer-locked captures")

    rows = analyze(ref, comp, lag)
    print(f"\n{'element':<18} {'expHz':>8} {'refHz':>8} {'refdB':>7} "
          f"{'compHz':>8} {'compdB':>7} {'ddB':>7}")
    diffs = []
    for label, exp_f, rf, rdb, cf, cdb, ddb in rows:
        diffs.append(ddb)
        print(f"{label:<18} {exp_f:>8.1f} {rf:>8.1f} {rdb:>7.1f} "
              f"{cf:>8.1f} {cdb:>7.1f} {ddb:>+7.2f}")
    if diffs:
        a = [abs(d) for d in diffs]
        print(f"\nmagnitude delta: mean |ddB|={sum(a)/len(a):.2f}  max |ddB|={max(a):.2f}")

    cmd = ["mdfourier", "-P", str(args.profile), "-r", str(args.ref), "-c", str(args.comp)]
    print("\nMDFourier command:\n  " + " ".join(cmd))
    if args.mdfourier:
        cmd[0] = str(args.mdfourier)
        print("running...")
        subprocess.run(cmd, check=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
