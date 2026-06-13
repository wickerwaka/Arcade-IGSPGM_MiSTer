"""T-ACC: ICS2115 accumulator-monitor / sample-memory readback mode.

Matrix row: 48-A.

The Turtle Beach firmware reads sample memory back through voice 0: program
OscAcc (0x0a/0x0b) to a sample address, set OscCtl=0x01 (monitor setup), then
read the data registers 0x48/0x49 and advance OscAcc.  PGM never uses this, so
the RTL is not expected to implement it — this test records what each target
returns so the divergence (and the real readback bytes on hardware) are
captured for a future RTL implementation.
"""

from __future__ import annotations

from util.ics2115_remote import WIDTH_16, WIDTH_LOWER8, WIDTH_UPPER8, Voice

from .harness import Emit, quiesce

MON_VOICE = 0
# A known music-ROM address (the BIOS-trace loop sample start) so the bytes
# read back are recognizable and cross-checkable against the ROM image.
BASE_ADDR = 0x2D640
N_BYTES = 16


def _setup_monitor(ics, addr: int) -> None:
    v = Voice()
    v.osc_conf = 0x00            # linear 8-bit class for byte-addressed readback
    v.vol_acc = 0xF000           # firmware StartAccumulatorMonitorRead value
    v.pan = 0x00
    v.set_acc_wave_addr(addr)    # OscAcc -> target sample address
    v.osc_ctl = 0x01            # accumulator-monitor setup
    ics.write_voice(MON_VOICE, v)


def _read_monitor_stream(ics, emit: Emit) -> None:
    # Walk consecutive addresses, reading the monitor data registers at each.
    _setup_monitor(ics, BASE_ADDR)
    reg48, reg49 = [], []
    for i in range(N_BYTES):
        # Re-point OscAcc each step (explicit, matching ReadAccumulatorMonitorByte
        # which manually increments 0x0a/0x0b).
        v = Voice()
        v.set_acc_wave_addr(BASE_ADDR + i)
        ics.write_reg(MON_VOICE, 0x0A, v.osc_acc_hi, width=WIDTH_16)
        ics.write_reg(MON_VOICE, 0x0B, v.osc_acc_lo, width=WIDTH_16)
        reg48.append(ics.read_reg(MON_VOICE, 0x48, width=WIDTH_LOWER8))
        reg49.append(ics.read_reg(MON_VOICE, 0x49, width=WIDTH_LOWER8))
    emit("t_acc.monitor_stream", {"base": BASE_ADDR, "n": N_BYTES},
         {"reg48": reg48, "reg49": reg49})


def _auto_increment(ics, emit: Emit) -> None:
    # Does reading the data register auto-advance OscAcc?  Set the address
    # once, then read 0x48 repeatedly and watch whether OscAcc moves.
    _setup_monitor(ics, BASE_ADDR)
    acc0 = ics.read_reg(MON_VOICE, 0x0A, width=WIDTH_16)
    reads = [ics.read_reg(MON_VOICE, 0x48, width=WIDTH_LOWER8) for _ in range(8)]
    acc1 = ics.read_reg(MON_VOICE, 0x0A, width=WIDTH_16)
    emit("t_acc.auto_increment", {"base": BASE_ADDR},
         {"reads": reads, "acc_before": acc0, "acc_after": acc1,
          "advanced": acc1 != acc0})


def run(ics, emit: Emit) -> None:
    quiesce(ics)
    _read_monitor_stream(ics, emit)
    _auto_increment(ics, emit)
    quiesce(ics)
