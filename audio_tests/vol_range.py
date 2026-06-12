from util.ics2115_remote import ICS2115Remote, Voice, VCtl, OscConf
import time

voice = Voice()
voice.set_acc_wave_addr(0x75750)
voice.set_start_wave_addr(0)
voice.set_end_wave_addr(0xffff0)
voice.osc_fc = 0
voice.vol_incr = 0xff
voice.vmode = 0x01
voice.vol_ctrl = 0
voice.osc_ctl = 0
voice.osc_conf = 0x08
voice.vol_acc = 0x0
voice.vol_start = 0x00
voice.vol_end = 0xff

with ICS2115Remote.open_sim() as ics:
    ics.sim.call("sim.run_cycles", {"count": 10_000_000})
    ics.play_voice(0, voice)

    for vol_end in range(256):
        ics.write_reg(0, "vol_ctrl", VCtl.Stop)
        ics.write_reg(0, "vol_acc", 0)
        ics.write_reg(0, "vol_start", 0)
        ics.write_reg(0, "vol_end", vol_end)
        ics.write_reg(0, "vol_ctrl", 0)

        while ics.read_reg(0, "vol_ctrl") & 1 == 0:
            ics.sim.call("sim.run_cycles", {"count": 100_000})

        print(hex(vol_end), hex(ics.read_reg(0, "vol_acc")))

