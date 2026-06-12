"""ICS2115 register-behavior conformance tests.

Each test group emits JSONL records that must be identical between the
simulator and real hardware (see docs/ics2115_register_matrix.md).  Run via
util/run_ics_reg_tests.py; compare runs via util/compare_ics_results.py.
"""

from . import t_irq, t_irqv, t_probe, t_probe_w, t_rb, t_tmr, t_voice

GROUPS = {
    "t_rb": t_rb.run,
    "t_probe": t_probe.run,
    "t_probe_w": t_probe_w.run,
    "t_tmr": t_tmr.run,
    "t_irq": t_irq.run,
    "t_irqv": t_irqv.run,
    "t_voice": t_voice.run,
}
