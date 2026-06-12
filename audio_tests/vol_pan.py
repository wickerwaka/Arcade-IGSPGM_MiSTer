from util.ics2115_remote import (ICS2115Remote, Voice)

ics = ICS2115Remote.open("pgm", reset="l")
ics.open_audio()

voice = Voice()
voice.set_acc_wave_addr(0x75750)
voice.set_start_wave_addr(0)
voice.set_end_wave_addr(0xffff0)
voice.osc_fc = 0
voice.vol_incr = 0
voice.vol_ctrl = 0x3
voice.osc_ctl = 0
voice.osc_conf = 0x08
voice.vol_acc = 0xffff
voice.vol_start = 0x00
voice.vol_end = 0xff

with open("pan_vol_test.csv", "wt") as fp:
    print("pan,vol_acc,left,right", file=fp)
    for pan in range(0, 256, 16):
        voice.pan = pan
        ics.play_voice(0, voice)

        fp.flush()

        for vol in range(0x0, 0x10000, 16):
            ics.write_reg(0, "vol_acc", vol)
            while True:
                sample = ics.latest_audio_samples(1)
                if len(sample) > 0:
                    break
                print("Sample capture failed")
            print(f"{pan},{vol},{sample[-1][0]},{sample[-1][1]}", file=fp)
            print(f"{pan},{vol},{sample[-1][0]},{sample[-1][1]}")

