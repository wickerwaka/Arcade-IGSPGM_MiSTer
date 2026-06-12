from util.ics2115_remote import (ICS2115Remote, Voice, OscConf, OscCtl, VCtl)
import time

ics = ICS2115Remote.open("pgm", reset="l")
ics.open_audio()

voice = Voice()
voice.set_acc_wave_addr(0x0)
voice.set_start_wave_addr(0)
voice.set_end_wave_addr(0x1000)
voice.osc_fc = 100
voice.vol_incr = 0
voice.vol_ctrl = VCtl.Rollover
voice.osc_ctl = OscCtl.KeyOff
voice.osc_conf = OscConf.Linear8
voice.vol_acc = 0x0000
voice.vol_start = 0x00
voice.vol_end = 0xff
voice.vol_incr = 1
voice.pan = 0x7f

ics.write_voice(0, voice)

ics.write_reg(0, "osc_ctl", 0x00)


def status_sleep():
    osc_addr = ics.read_reg(0, "osc_acc_hi")
    vol_acc = ics.read_reg(0, "vol_acc")
    osc_ctl = ics.read_reg(0, "osc_ctl")
    vol_ctl = ics.read_reg(0, "vol_ctrl")
    sample = ics.latest_audio_samples(1)
    print(osc_addr, hex(osc_ctl), hex(vol_ctl), vol_acc, sample[0])
    time.sleep(.5)


for _ in range(5):
    status_sleep()

ics.write_reg(0, "osc_ctl", 0x01)

for _ in range(5):
    status_sleep()

ics.write_reg(0, "osc_ctl", 0x00)

for _ in range(5):
    status_sleep()


