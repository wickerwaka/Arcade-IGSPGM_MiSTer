from pathlib import Path

from util.ics2115_remote import ICS2115Remote, Voice, VCtl, OscConf

voice = Voice()
voice.set_acc_wave_addr(0x75750)
voice.set_start_wave_addr(0)
voice.set_end_wave_addr(0xffff0)
voice.osc_fc = 0
voice.vol_incr = 0x0f
voice.vmode = 0x01
voice.vol_ctrl = VCtl.Loop
voice.osc_ctl = 0
voice.osc_conf = 0x08
voice.vol_acc = 0x0
voice.vol_start = 0x00
voice.vol_end = 0xff

fout = open("vol_incr_more_samples.csv", "wt")
with ICS2115Remote.open("pgm", reset="l") as ics:
    audio = ics.open_audio()
    ics.play_voice(0, voice)

    for vmode in range(1):
        fout.flush()
        for incr in range(256):
            print(f"vmode={vmode},vincr={incr}")
            ics.write_reg(0, "vol_ctrl", VCtl.Stop)
            ics.write_reg(0, "vol_acc", 0)
            ics.write_reg(0, "vol_start", 0)
            ics.write_reg(0, "vol_end", 0xff)
            ics.write_reg(0, "vol_incr", incr)
            ics.write_reg(0, "vmode", vmode)
            ics.write_reg(0, "vol_ctrl", VCtl.Loop)
            row = [ vmode, incr ]
            row.extend([ x[0] for x in ics.latest_audio_samples(8 * 32 * 1024, blocks=2048, timeout=30) ])
            print(",".join([str(x) for x in row]), file=fout)
fout.close()

