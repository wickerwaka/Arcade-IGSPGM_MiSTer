from util.ics2115_remote import ICS2115Remote, Voice, VCtl, OscConf

try:
    from .disk_cache import disk_cache
except ImportError:
    from disk_cache import disk_cache


@disk_cache
def pgm_capture_samples(voice, count):
    pgm_samples = []
    with ICS2115Remote.open("pgm", reset="l") as ics:
        ics.open_audio()
        for x in range(count):
            voice.osc_acc_lo = x << 4
            ics.play_voice(0, voice)
            sample = ics.capture_audio_frames(64)[-1][0]
            pgm_samples.append((voice.osc_acc_lo, sample))

    return pgm_samples


@disk_cache
def sim_capture_samples(voice, count):
    sim_samples = []
    with ICS2115Remote.open_sim() as ics:
        ics.sim.call("sim.run_cycles", {"count": 10_000_000})
        ics.sim.call("trace.start", {"filename": "audio_16.fst"})
        ics.open_audio()
        for x in range(count):
            voice.osc_acc_lo = x << 4
            ics.play_voice(0, voice)
            sim_samples.append(ics.capture_audio_frames(64)[-1][0])
        ics.sim.call("trace.stop", {})
    return sim_samples

@disk_cache
def pgm_capture_acc(voice, count):
    acc = 0
    with ICS2115Remote.open("pgm", reset="l") as ics:
        ics.play_voice(0, voice)
        for x in range(count):
            foo = ics.read_reg(0, "osc_acc_lo")
            acc = acc | foo
            print(bin(acc))

    return acc


voice = Voice()
voice.set_acc_wave_addr(0x0)
voice.set_start_wave_addr(0)
voice.set_end_wave_addr(0xffffff)
voice.osc_fc = 0x0
voice.vol_incr = 0x00
voice.vmode = 0x00
voice.vol_ctrl = 0x3
voice.osc_ctl = 0
voice.osc_conf = OscConf.Linear16
voice.vol_acc = 0xffff
voice.vol_start = 0x00
voice.vol_end = 0xff

#voice.set_acc_wave_addr(0x0)
#pgm_capture_samples(voice, 4096)

voice.set_acc_wave_addr(0x0)
voice.osc_fc = 0x2
pgm_capture_acc(voice, 4096)

#voice.set_acc_wave_addr(0x0)
#print(sim_capture_samples(voice, 32))

