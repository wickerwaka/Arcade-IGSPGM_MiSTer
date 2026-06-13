"""ICS2115 register-behavior conformance tests.

Each test group emits JSONL records that must be identical between the
simulator and real hardware (see docs/ics2115_register_matrix.md).  Run via
util/run_ics_reg_tests.py; compare runs via util/compare_ics_results.py.
"""

from . import t_acc, t_aud, t_irq, t_irqv, t_octl, t_probe, t_probe_w, t_rate, t_rb, t_sys, t_tmr, t_vincr, t_vincr2, t_voice

GROUPS = {
    "t_rb": t_rb.run,
    "t_probe": t_probe.run,
    "t_probe_w": t_probe_w.run,
    "t_tmr": t_tmr.run,
    "t_irq": t_irq.run,
    "t_irqv": t_irqv.run,
    "t_voice": t_voice.run,
    "t_aud": t_aud.run,
    "t_octl": t_octl.run,
    "t_sys": t_sys.run,
    "t_acc": t_acc.run,
    "t_rate": t_rate.run,
    "t_vincr": t_vincr.run,
    "t_vincr2": t_vincr2.run,
}
