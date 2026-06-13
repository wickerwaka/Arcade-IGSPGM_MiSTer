#!/usr/bin/env python3
"""Check that hscale parameters only latch at vblank."""
from hscale_verify import SimServer

sim = SimServer()
sim.call("sim.initialize", {"headless": True})
sim.call("sim.load_game", {"name": "pgm"})
sim.call("sim.reset", {"cycles": 100})
sim.call("sim.run_frames", {"count": 30}, quiet=True)

sim.call("video.set_hscale", {"enabled": True, "scale": 0, "offset": 0})
sim.call("sim.run_frames", {"count": 3}, quiet=True)


def ticks():
    return sim.call("sim.status", quiet=True)["total_ticks"]


def wait(signal, value, timeout=2000000):
    sim.call("sim.run_until", {
        "condition": {"type": "signal_equals", "signal": signal, "value": value},
        "timeout_cycles": timeout,
    }, quiet=True)
    return ticks()


def measure_line():
    wait("hsync", 0)
    t_rise = wait("hsync", 1)
    wait("hsync", 0)
    t_active = wait("hblank", 0)
    return t_active - t_rise


failures = []


def check(name, cond, detail=""):
    print(f"[{'PASS' if cond else 'FAIL'}] {name} {detail}")
    if not cond:
        failures.append(name)


# Move to mid-frame: just after vblank ends, plenty of lines remain
wait("vblank", 1)
wait("vblank", 0)
check("k=0 geometry before change", measure_line() == 661)

# Change scale mid-frame: lines in the same frame must keep old geometry
sim.call("video.set_hscale", {"enabled": True, "scale": -16, "offset": 0}, quiet=True)
d = measure_line()
check("mid-frame change not applied", d == 661, f"hs->active {d}")

# After the next vblank the new geometry must be in effect
wait("vblank", 1)
wait("vblank", 0)
d = measure_line()
check("applied after vblank", d == 885, f"hs->active {d}")

sim.close()
print("ALL PASS" if not failures else f"FAILURES: {failures}")
exit(1 if failures else 0)
