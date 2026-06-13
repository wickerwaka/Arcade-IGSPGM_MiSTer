#!/usr/bin/env python3
"""Drive the sim server to verify the video_hscale module."""
import json
import subprocess
import sys
import os
import struct

SIM_DIR = os.path.dirname(os.path.abspath(__file__))


class SimServer:
    def __init__(self):
        self.proc = subprocess.Popen(
            ["./sim", "--server"],
            cwd=SIM_DIR,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        self.next_id = 1

    def call(self, method, params=None, quiet=False):
        req = {"id": self.next_id, "method": method, "params": params or {}}
        self.next_id += 1
        self.proc.stdin.write(json.dumps(req) + "\n")
        self.proc.stdin.flush()
        line = self.proc.stdout.readline()
        if not line:
            raise RuntimeError(f"server died on {method}")
        resp = json.loads(line)
        if not resp.get("ok"):
            raise RuntimeError(f"{method} failed: {resp}")
        if not quiet:
            print(f"  {method} -> ok")
        return resp.get("result", {})

    def close(self):
        try:
            self.call("sim.shutdown", quiet=True)
        except Exception:
            pass
        self.proc.stdin.close()
        self.proc.wait(timeout=10)


def png_pixels(path):
    """Return (width, height, rows of RGB tuples) via sips BMP conversion."""
    bmp = path + ".bmp"
    subprocess.run(
        ["sips", "-s", "format", "bmp", path, "--out", bmp],
        check=True, capture_output=True,
    )
    with open(bmp, "rb") as f:
        data = f.read()
    os.unlink(bmp)
    off = struct.unpack_from("<I", data, 10)[0]
    w = struct.unpack_from("<i", data, 18)[0]
    h = struct.unpack_from("<i", data, 22)[0]
    bpp = struct.unpack_from("<H", data, 28)[0]
    assert bpp in (24, 32), f"unexpected bpp {bpp}"
    nb = bpp // 8
    stride = (w * nb + 3) & ~3
    flipped = h > 0
    h = abs(h)
    rows = []
    for y in range(h):
        src_y = (h - 1 - y) if flipped else y
        base = off + src_y * stride
        row = []
        for x in range(w):
            b, g, r = data[base + x * nb:base + x * nb + 3]
            row.append((r, g, b))
        rows.append(row)
    return w, h, rows


def active_width(rows):
    """Max extent of non-black pixels across all rows."""
    lo, hi = None, None
    for row in rows:
        for x, px in enumerate(row):
            if px != (0, 0, 0):
                if lo is None or x < lo:
                    lo = x
                if hi is None or x > hi:
                    hi = x
    return (lo, hi) if lo is not None else (None, None)


def content_signature(rows, decimate=1, phase=0):
    """Rows downsampled by 'decimate' starting at 'phase'."""
    return [tuple(row[phase::decimate]) for row in rows]


def main():
    sim = SimServer()
    failures = []

    def check(name, cond, detail=""):
        status = "PASS" if cond else "FAIL"
        print(f"[{status}] {name} {detail}")
        if not cond:
            failures.append(name)

    print("== startup ==")
    sim.call("sim.initialize", {"headless": True})
    sim.call("sim.load_game", {"name": "pgm"})
    sim.call("sim.reset", {"cycles": 100})

    # Run until the BIOS draws something
    base_png = "/tmp/hs_base.png"
    for i in range(20):
        sim.call("sim.run_frames", {"count": 30}, quiet=True)
        sim.call("video.screenshot", {"path": base_png}, quiet=True)
        w, h, rows = png_pixels(base_png)
        lo, hi = active_width(rows)
        if lo is not None and hi - lo > 100:
            print(f"  content after {(i + 1) * 30} frames, extent {lo}..{hi}")
            break
    else:
        print("no visible content; aborting")
        sim.close()
        sys.exit(1)

    # Wait for a static screen so cross-mode pixel comparisons see the
    # same image (the BIOS check screen animates for a while)
    prev = rows
    for i in range(60):
        sim.call("sim.run_frames", {"count": 5}, quiet=True)
        sim.call("video.screenshot", {"path": base_png}, quiet=True)
        w, h, rows = png_pixels(base_png)
        if rows == prev:
            print(f"  screen static after {i + 1} settle iterations")
            break
        prev = rows
    else:
        print("  warning: screen never settled; comparisons may be unstable")

    base_w, base_h, base_rows = png_pixels(base_png)
    check("baseline size", (base_w, base_h) == (448, 224), f"{base_w}x{base_h}")

    def set_mode(scale, offset):
        # First call switches the inputs (latched at next vblank); second
        # call re-clears the framebuffer once the new geometry is active,
        # so no transition-frame pixels linger in the screenshot
        sim.call("video.set_hscale", {"enabled": True, "scale": scale, "offset": offset})
        sim.call("sim.run_frames", {"count": 2}, quiet=True)
        sim.call("video.set_hscale", {"enabled": True, "scale": scale, "offset": offset})
        sim.call("sim.run_frames", {"count": 2}, quiet=True)

    print("== scaler k=0 (100%) ==")
    set_mode(0, 0)
    k0_png = "/tmp/hs_k0.png"
    sim.call("video.screenshot", {"path": k0_png}, quiet=True)
    w, h, rows = png_pixels(k0_png)
    check("k=0 fb size", (w, h) == (2688, 224), f"{w}x{h}")
    lo, hi = active_width(rows)
    base_lo, base_hi = active_width(base_rows)
    print(f"  k=0 extent {lo}..{hi} (baseline {base_lo}..{base_hi})")
    # Each native pixel should repeat exactly 5x at k=0
    check("k=0 width", lo is not None and (hi - lo + 1) == 5 * (base_hi - base_lo + 1),
          f"got {hi - lo + 1}, want {5 * (base_hi - base_lo + 1)}")
    # Nearest-neighbour identity: each native pixel repeats exactly 5x at
    # k=0, so sampling every 5th column from the active start must
    # reproduce the baseline active region exactly
    cand = [tuple(row[lo:hi + 1][::5]) for row in rows]
    refc = [tuple(row[base_lo:base_hi + 1]) for row in base_rows]
    check("k=0 5:1 decimation identity", cand == refc)

    print("== scaler k=-16 (80%) ==")
    set_mode(-16, 0)
    p = "/tmp/hs_km16.png"
    sim.call("video.screenshot", {"path": p}, quiet=True)
    w, h, rows = png_pixels(p)
    lo, hi = active_width(rows)
    expect = (base_hi - base_lo + 1) * 4  # 80% of 5x
    print(f"  k=-16 extent {lo}..{hi}")
    check("k=-16 width", lo is not None and abs((hi - lo + 1) - expect) <= 4,
          f"got {hi - lo + 1}, want ~{expect}")

    print("== scaler k=+15 (118.75%) ==")
    set_mode(15, 0)
    p = "/tmp/hs_kp15.png"
    sim.call("video.screenshot", {"path": p}, quiet=True)
    w, h, rows = png_pixels(p)
    lo, hi = active_width(rows)
    expect = round((base_hi - base_lo + 1) * 5 * 95 / 80)
    print(f"  k=+15 extent {lo}..{hi}")
    check("k=+15 width", lo is not None and abs((hi - lo + 1) - expect) <= 4,
          f"got {hi - lo + 1}, want ~{expect}")

    print("== hsync timing ==")

    def ticks():
        return sim.call("sim.status", quiet=True)["total_ticks"]

    def wait(signal, value, timeout=20000):
        sim.call("sim.run_until", {
            "condition": {"type": "signal_equals", "signal": signal, "value": value},
            "timeout_cycles": timeout,
        }, quiet=True)
        return ticks()

    def measure(k, off):
        sim.call("video.set_hscale", {"enabled": True, "scale": k, "offset": off})
        sim.call("sim.run_frames", {"count": 2}, quiet=True)  # latch at vblank
        wait("hsync", 0)
        t_rise = wait("hsync", 1)
        t_fall = wait("hsync", 0)
        t_active = wait("hblank", 0)
        wait("hsync", 1)
        t_rise2 = ticks()
        width = t_fall - t_rise
        period = t_rise2 - t_rise
        hs_to_active = t_active - t_rise
        abs_k = abs(k)
        exp_d = (976 + (28 * abs_k if k < 0 else 0)) - (315 + 14 * abs_k + 5 * off)
        check(f"hs width   k={k:+d} off={off:+d}", width == 315, f"got {width}")
        check(f"hs period  k={k:+d} off={off:+d}", period == 3200, f"got {period}")
        check(f"hs->active k={k:+d} off={off:+d}", hs_to_active == exp_d,
              f"got {hs_to_active}, want {exp_d}")

    for k, off in [(0, 0), (-16, 0), (15, 0), (-16, -16), (-16, 15), (15, -16), (15, 15)]:
        measure(k, off)

    print("== debug flags ==")
    for flag in ("debug_underrun", "debug_overflow"):
        value = None
        for name in (f"video_hscale.{flag}", f"sim_top.video_hscale.{flag}"):
            try:
                value = sim.call("signal.read", {"name": name}, quiet=True).get("value")
                break
            except RuntimeError:
                continue
        check(flag, value == 0, f"value={value}")

    print("== disable restores native output ==")
    sim.call("video.set_hscale", {"enabled": False, "scale": 0, "offset": 0})
    sim.call("sim.run_frames", {"count": 3}, quiet=True)
    p = "/tmp/hs_off.png"
    sim.call("video.screenshot", {"path": p}, quiet=True)
    w, h, rows = png_pixels(p)
    lo, hi = active_width(rows)
    check("disabled size", (w, h) == (448, 224), f"{w}x{h}")
    check("disabled extent", (lo, hi) == (base_lo, base_hi), f"{lo}..{hi}")

    sim.close()
    print()
    if failures:
        print(f"FAILURES: {failures}")
        sys.exit(1)
    print("ALL PASS")


if __name__ == "__main__":
    main()
