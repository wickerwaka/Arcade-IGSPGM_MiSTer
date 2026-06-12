#!/usr/bin/env python3
"""Generate dependency-free PNG heatmaps from vol_incr summaries."""

from __future__ import annotations

import argparse
import csv
import struct
import zlib
from pathlib import Path


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def write_png(path: Path, img: list[list[tuple[int, int, int]]]) -> None:
    height = len(img)
    width = len(img[0]) if height else 0
    raw = b"".join(b"\x00" + bytes(c for pixel in row for c in pixel) for row in img)
    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += png_chunk(b"IDAT", zlib.compress(raw, 6))
    png += png_chunk(b"IEND", b"")
    path.write_bytes(png)


def color_ramp(t: float) -> tuple[int, int, int]:
    t = max(0.0, min(1.0, t))
    # dark blue -> cyan -> yellow -> red
    if t < 0.33:
        u = t / 0.33
        return (0, int(80 + 120 * u), int(120 + 100 * u))
    if t < 0.66:
        u = (t - 0.33) / 0.33
        return (int(255 * u), 220, int(220 * (1 - u)))
    u = (t - 0.66) / 0.34
    return (255, int(220 * (1 - u)), 0)


def load_summary(path: Path) -> list[dict]:
    rows = []
    with path.open(newline="") as f:
        for row in csv.DictReader(f):
            parsed = dict(row)
            for key in ("vmode", "vincr", "sample_count", "max_gain", "unique_gain_count", "transition_count", "reset_count"):
                parsed[key] = int(parsed[key]) if parsed[key] else 0
            for key in ("reset_period_est", "approx_index_rate"):
                parsed[key] = float(parsed[key]) if parsed[key] else None
            rows.append(parsed)
    return rows


def make_heatmap(rows: list[dict], field: str, path: Path, *, cell: int = 5) -> None:
    vmodes = sorted({r["vmode"] for r in rows})
    vincrs = sorted({r["vincr"] for r in rows})
    values = [r[field] for r in rows if r.get(field) is not None]
    if not values:
        raise ValueError(f"no values for {field}")
    vmin = min(values)
    vmax = max(values)
    if vmax == vmin:
        vmax = vmin + 1
    lookup = {(r["vmode"], r["vincr"]): r.get(field) for r in rows}
    width = len(vincrs) * cell
    height = len(vmodes) * cell
    img = [[(255, 255, 255) for _ in range(width)] for _ in range(height)]
    for y, vmode in enumerate(vmodes):
        for x, vincr in enumerate(vincrs):
            value = lookup.get((vmode, vincr))
            color = (230, 230, 230) if value is None else color_ramp((value - vmin) / (vmax - vmin))
            for yy in range(y * cell, (y + 1) * cell):
                for xx in range(x * cell, (x + 1) * cell):
                    img[yy][xx] = color
    write_png(path, img)
    print(f"wrote {path} ({field}, min={vmin}, max={vmax})")


def main() -> int:
    ap = argparse.ArgumentParser(description="Plot vol_incr summary heatmaps")
    ap.add_argument("summary", nargs="?", default="audio_tests/vol_incr.summary.csv")
    ap.add_argument("--out-dir", default="audio_tests")
    ap.add_argument("--cell", type=int, default=5)
    args = ap.parse_args()

    rows = load_summary(Path(args.summary))
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for field in ("transition_count", "reset_count", "reset_period_est", "approx_index_rate", "max_gain"):
        make_heatmap(rows, field, out_dir / f"vol_incr_{field}.png", cell=args.cell)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
