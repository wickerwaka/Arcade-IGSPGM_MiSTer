#!/usr/bin/env python3
"""Generate an inventory of ICS2115 register access sites in the PGM-BIOS decompiles.

Scans Ghidra decompile text exports for calls to the eight named ICS helper
functions and emits a CSV that docs/ics2115_register_matrix.md cites by row.
Re-run and diff against the committed CSV to verify the audit:

    python3 util/ics_access_inventory.py --check

Sources scanned (text decompiles only; theglad/espgaluda game drivers exist
only as Ghidra projects and are audited separately via ghidra-mcp):
    ~/Source/PGM-BIOS/pgm_bios_z80_raw_decompiled.c
"""

from __future__ import annotations

import argparse
import csv
import io
import re
import sys
from pathlib import Path

DEFAULT_BIOS_ROOT = Path.home() / "Source" / "PGM-BIOS"
DEFAULT_OUTPUT = Path(__file__).resolve().parent.parent / "docs" / "ics2115_access_inventory.csv"

SOURCES = [
    ("pgm_bios_z80", "pgm_bios_z80_raw_decompiled.c"),
]

# helper name -> (rw, width, fixed_reg)
HELPERS = {
    "WriteICSRegisterByteLow": ("W", "LOWER8", None),
    "WriteICSRegisterByteHigh": ("W", "UPPER8", None),
    "WriteICSRegisterWord": ("W", "WORD", None),
    "ReadICSRegisterByteLow": ("R", "LOWER8", None),
    "ReadICSRegisterHigh": ("R", "UPPER8", None),
    "ReadICSRegisterWord": ("R", "WORD", None),
    # Selects the voice via the register-select port; behaves as a write of 0x4f.
    "WriteICSSelectOscillator": ("W", "LOWER8", "0x4f"),
    # Direct read of the Z80 status port 0x8000 (not an indexed register).
    "ReadIrqStatusPort": ("R", "PORT8", "STATUS_PORT"),
}

CALL_RE = re.compile(r"\b(" + "|".join(HELPERS) + r")\s*\(([^;]*)\)\s*;")
DEF_RE = re.compile(r"^(?:byte|ushort|void)\s+(?:" + "|".join(HELPERS) + r")\s*\(")
FUNC_HEADER_RE = re.compile(r"^\s*\*\s+ram:([0-9a-f]{4})\s+(\S+)")
INT_RE = re.compile(r"^(0x[0-9a-fA-F]+|\d+)$")

CSV_FIELDS = ["source", "file", "line", "function", "reg", "width", "rw", "value", "notes"]


def parse_literal(text: str) -> str | None:
    """Return canonical 0xNN form if text is an integer literal, else None."""
    text = text.strip()
    m = INT_RE.match(text)
    if not m:
        return None
    return f"0x{int(m.group(1), 0):02x}"


def split_args(arg_text: str) -> list[str]:
    """Split a C argument list on top-level commas."""
    args, depth, cur = [], 0, []
    for ch in arg_text:
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        if ch == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
        else:
            cur.append(ch)
    tail = "".join(cur).strip()
    if tail:
        args.append(tail)
    return args


def scan_file(source: str, path: Path) -> list[dict]:
    rows = []
    current_func = "?"
    for lineno, line in enumerate(path.read_text().splitlines(), start=1):
        header = FUNC_HEADER_RE.match(line)
        if header:
            current_func = f"{header.group(2)}@ram:{header.group(1)}"
            continue
        if DEF_RE.match(line) or line.lstrip().startswith("*"):
            continue
        for m in CALL_RE.finditer(line):
            helper, arg_text = m.group(1), m.group(2)
            rw, width, fixed_reg = HELPERS[helper]
            args = split_args(arg_text)
            notes = []

            if fixed_reg is not None:
                reg = fixed_reg
                value_arg = args[0] if args else ""
                notes.append(helper)
            else:
                reg_arg = args[0] if args else ""
                reg = parse_literal(reg_arg)
                if reg is None:
                    reg = "DYNAMIC"
                    notes.append(f"reg expr: {reg_arg}")
                value_arg = args[1] if len(args) > 1 else ""

            if rw == "W":
                value = parse_literal(value_arg)
                if value is None:
                    value = "DYNAMIC"
                    notes.append(f"value expr: {value_arg}")
            else:
                value = ""

            rows.append({
                "source": source,
                "file": path.name,
                "line": lineno,
                "function": current_func,
                "reg": reg,
                "width": width,
                "rw": rw,
                "value": value,
                "notes": "; ".join(notes),
            })
    return rows


def render_csv(rows: list[dict]) -> str:
    buf = io.StringIO()
    writer = csv.DictWriter(buf, fieldnames=CSV_FIELDS, lineterminator="\n")
    writer.writeheader()
    for row in rows:
        writer.writerow(row)
    return buf.getvalue()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--bios-root", type=Path, default=DEFAULT_BIOS_ROOT)
    ap.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    ap.add_argument("--check", action="store_true",
                    help="verify the committed CSV matches a fresh scan instead of writing")
    args = ap.parse_args()

    rows = []
    for source, name in SOURCES:
        path = args.bios_root / name
        if not path.is_file():
            print(f"error: missing source {path}", file=sys.stderr)
            return 1
        rows.extend(scan_file(source, path))
    rows.sort(key=lambda r: (r["source"], r["file"], r["line"]))

    content = render_csv(rows)
    by_reg: dict[str, int] = {}
    for row in rows:
        by_reg[row["reg"]] = by_reg.get(row["reg"], 0) + 1
    summary = ", ".join(f"{reg}:{n}" for reg, n in sorted(by_reg.items()))
    print(f"{len(rows)} call sites; per-reg counts: {summary}", file=sys.stderr)

    if args.check:
        if not args.output.is_file():
            print(f"error: {args.output} does not exist", file=sys.stderr)
            return 1
        if args.output.read_text() != content:
            print(f"error: {args.output} is stale; re-run without --check", file=sys.stderr)
            return 1
        print(f"OK: {args.output} matches a fresh scan", file=sys.stderr)
        return 0

    args.output.write_text(content)
    print(f"wrote {args.output}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
