import base64
import math
import os
import struct
import sys
import zlib
import time
from pathlib import Path

from util.ics2115_remote import ICS2115Remote, Voice, VCtl, OscConf

def trim_samples(samples):
    zero = (0, 0)
    start = 0

    if samples and samples[0] != zero:
        try:
            start = samples.index(zero)
        except ValueError:
            return []

    while start < len(samples) and samples[start] == zero:
        start += 1

    end = len(samples)
    while end > start and samples[end - 1] == zero:
        end -= 1

    return samples[start:end]


def _rms(values):
    if not values:
        return 0.0
    return math.sqrt(sum(v * v for v in values) / len(values))


def _channel_stats(ref, test, channel):
    r = [s[channel] for s in ref]
    t = [s[channel] for s in test]
    diffs = [b - a for a, b in zip(r, t)]
    ref_rms = _rms(r)
    test_rms = _rms(t)
    diff_rms = _rms(diffs)
    diff_max = max((abs(d) for d in diffs), default=0)
    ref_mean = sum(r) / len(r) if r else 0.0
    test_mean = sum(t) / len(t) if t else 0.0

    denom = sum(a * a for a in r)
    gain = (sum(a * b for a, b in zip(r, t)) / denom) if denom else 0.0

    r_centered = [a - ref_mean for a in r]
    t_centered = [b - test_mean for b in t]
    corr_denom = math.sqrt(sum(a * a for a in r_centered) * sum(b * b for b in t_centered))
    corr = (sum(a * b for a, b in zip(r_centered, t_centered)) / corr_denom) if corr_denom else 0.0

    return {
        "ref_rms": ref_rms,
        "test_rms": test_rms,
        "gain": gain,
        "gain_db": 20.0 * math.log10(abs(gain)) if gain else float("-inf"),
        "corr": corr,
        "dc_offset": test_mean - ref_mean,
        "diff_max": diff_max,
        "diff_rms": diff_rms,
        "diff_norm": diff_rms / ref_rms if ref_rms else 0.0,
    }


def compare_trimmed_samples(ref_samples, test_samples, *, print_report=True):
    """Compare two already-trimmed stereo sample lists.

    The comparison length is the smaller input length. Returns a dict and,
    by default, prints a compact report in the style of compare_audio_captures.py.
    """
    total = min(len(ref_samples), len(test_samples))
    ref = ref_samples[:total]
    test = test_samples[:total]

    ref_flat = [v for s in ref for v in s]
    test_flat = [v for s in test for v in s]
    diffs = [b - a for a, b in zip(ref_flat, test_flat)]
    ref_rms = _rms(ref_flat)
    diff_rms = _rms(diffs)
    diff_max = max((abs(d) for d in diffs), default=0)
    exact = sum(1 for a, b in zip(ref_flat, test_flat) if a == b)

    left = _channel_stats(ref, test, 0)
    right = _channel_stats(ref, test, 1)
    gain = (left["gain"] + right["gain"]) / 2.0
    corr = (left["corr"] + right["corr"]) / 2.0

    categories = []
    if not ref_samples:
        categories.append("reference_silent_or_empty")
    if not test_samples:
        categories.append("test_silent_or_empty")
    if len(ref_samples) != len(test_samples):
        categories.append("length_mismatch")
    if abs(left["dc_offset"]) > 1.0 or abs(right["dc_offset"]) > 1.0:
        categories.append("dc_offset_difference")
    if gain and abs(gain - 1.0) > 0.02:
        categories.append("amplitude_gain_mismatch")
    elif gain and abs(gain - 1.0) > 0.002:
        categories.append("minor_amplitude_gain_difference")
    if abs(left["gain"] - right["gain"]) > 0.01:
        categories.append("channel_balance_mismatch")
    norm = diff_rms / ref_rms if ref_rms else 0.0
    if diff_max == 0:
        categories.append("bit_exact")
    elif norm < 0.001:
        categories.append("close_match")
    elif norm < 0.01:
        categories.append("small_waveform_difference")
    elif norm < 0.05:
        categories.append("moderate_waveform_difference")
    else:
        categories.append("large_waveform_difference")

    result = {
        "categories": categories,
        "input_lengths": {"ref": len(ref_samples), "test": len(test_samples)},
        "compared_frames": total,
        "overall": {
            "gain": gain,
            "corr": corr,
            "diff_max": diff_max,
            "diff_rms": diff_rms,
            "diff_norm": norm,
            "exact_values": exact,
            "total_values": len(ref_flat),
        },
        "channels": {"left": left, "right": right},
    }

    if print_report:
        print("Sample comparison")
        print(f"  categories: {', '.join(categories)}")
        print(f"  lengths: ref={len(ref_samples)} test={len(test_samples)} compared={total}")
        print("Overall")
        print(f"  gain={gain:.4f} corr={corr:.4f}")
        print(f"  diff_max={diff_max} diff_rms={diff_rms:.2f} norm={norm:.4f}")
        print(f"  exact_values={exact}/{len(ref_flat)}")
        for name, stats in (("Left", left), ("Right", right)):
            print(name)
            print(f"  gain={stats['gain']:.4f} corr={stats['corr']:.4f} dc={stats['dc_offset']:.2f}")
            print(f"  diff_max={stats['diff_max']} diff_rms={stats['diff_rms']:.2f} norm={stats['diff_norm']:.4f}")

    return result


def _png_chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def _draw_line(img, x0, y0, x1, y1, color):
    height = len(img)
    width = len(img[0])
    dx = abs(x1 - x0)
    sx = 1 if x0 < x1 else -1
    dy = -abs(y1 - y0)
    sy = 1 if y0 < y1 else -1
    err = dx + dy

    while True:
        if 0 <= x0 < width and 0 <= y0 < height:
            img[y0][x0] = color
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def write_comparison_png(path, ref_samples, test_samples, *, width=1800, height=520):
    """Write a dependency-free PNG overlay plot for two trimmed sample lists."""
    total = min(len(ref_samples), len(test_samples))
    if total == 0:
        raise ValueError("nothing to plot")

    ref = ref_samples[:total]
    test = test_samples[:total]
    ref_mono = [(l + r) / 2.0 for l, r in ref]
    test_mono = [(l + r) / 2.0 for l, r in test]
    diff = [abs(b - a) for a, b in zip(ref_mono, test_mono)]

    img = [[(255, 255, 255) for _ in range(width)] for _ in range(height)]
    wave_mid = height // 3
    diff_mid = (height * 2) // 3
    wave_scale = max(max(abs(v) for v in ref_mono), max(abs(v) for v in test_mono), 1.0)
    diff_scale = max(diff, default=1.0) or 1.0
    wave_amp = height * 0.28
    diff_amp = height * 0.25

    for x in range(width):
        img[wave_mid][x] = (210, 210, 210)
        img[diff_mid][x] = (210, 210, 210)

    def sample_index(x):
        return min(total - 1, int(x * total / width))

    prev_ref = prev_test = prev_diff = None
    for x in range(width):
        i = sample_index(x)
        ref_y = max(0, min(height - 1, int(wave_mid - ref_mono[i] / wave_scale * wave_amp)))
        test_y = max(0, min(height - 1, int(wave_mid - test_mono[i] / wave_scale * wave_amp)))
        diff_y = max(0, min(height - 1, int(diff_mid - diff[i] / diff_scale * diff_amp)))

        if prev_ref is not None:
            _draw_line(img, x - 1, prev_ref, x, ref_y, (40, 90, 220))
            _draw_line(img, x - 1, prev_test, x, test_y, (220, 80, 40))
            _draw_line(img, x - 1, prev_diff, x, diff_y, (20, 160, 60))
        prev_ref, prev_test, prev_diff = ref_y, test_y, diff_y

    raw = b"".join(b"\x00" + bytes(channel for pixel in row for channel in pixel) for row in img)
    png = b"\x89PNG\r\n\x1a\n"
    png += _png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += _png_chunk(b"IDAT", zlib.compress(raw, 6))
    png += _png_chunk(b"IEND", b"")
    Path(path).write_bytes(png)


def display_png_if_supported(path):
    """Inline-display PNG in iTerm2/Kitty terminals when stdout is a TTY."""
    if not sys.stdout.isatty():
        return False

    data = Path(path).read_bytes()
    encoded = base64.b64encode(data).decode("ascii")

    if os.environ.get("TERM_PROGRAM") == "iTerm.app":
        name = base64.b64encode(Path(path).name.encode()).decode("ascii")
        print(f"\033]1337;File=name={name};inline=1;size={len(data)}:{encoded}\a")
        return True

    if True: #os.environ.get("KITTY_WINDOW_ID"):
        chunk_size = 4096
        for offset in range(0, len(encoded), chunk_size):
            chunk = encoded[offset:offset + chunk_size]
            more = 1 if offset + chunk_size < len(encoded) else 0
            prefix = "\033_Gf=100,a=T" if offset == 0 else "\033_G"
            print(f"{prefix},m={more};{chunk}\033\\", end="")
        print()
        return True

    return False


voice = Voice()
voice.set_acc_wave_addr(0x75750)
voice.set_start_wave_addr(0)
voice.set_end_wave_addr(0xffff0)
voice.osc_fc = 0
voice.vol_incr = 0x00
voice.vmode = 0x00
voice.vol_ctrl = 0x3
voice.osc_ctl = 0
voice.osc_conf = OscConf.ULaw8 | OscConf.Loop
voice.vol_acc = 0xffff
voice.vol_start = 0x00
voice.vol_end = 0xff

voice = Voice()
voice.set_acc_wave_addr(0x00)
voice.set_start_wave_addr(0)
voice.set_end_wave_addr(0xf)
voice.osc_fc = 0x10
voice.vol_incr = 0x00
voice.vmode = 0x00
voice.vol_ctrl = 0x3
voice.osc_ctl = 0
voice.osc_conf = OscConf.Linear16 | OscConf.Loop
voice.vol_acc = 0xffff
voice.vol_start = 0x00
voice.vol_end = 0xff


with ICS2115Remote.open("pgm", reset="l") as ics:
    ics.write_reg(0, "osc_ctl", 0x0f)
    audio = ics.open_audio()
    ics.play_voice(0, voice)
    pgm_samples = trim_samples(audio.read_latest_samples(2048))
    print(pgm_samples)

with ICS2115Remote.open_sim() as ics:
    ics.sim.call("sim.run_cycles", {"count": 10_000_000})

    ics.open_audio()
    sim_samples = ics.capture_audio_frames(16)
    ics.play_voice(0, voice)
    #ics.sim.call("trace.start", {"filename": "audio.fst"})
    sim_samples.extend(ics.capture_audio_frames(2048 - 16))
    #ics.sim.call("trace.stop", {})
    sim_samples = trim_samples(sim_samples)
    print(sim_samples)

compare_trimmed_samples(pgm_samples, sim_samples)
plot_path = Path(__file__).with_name("sim_vs_pgm.png")
write_comparison_png(plot_path, pgm_samples, sim_samples)
print(f"Wrote plot: {plot_path}")
if not display_png_if_supported(plot_path):
    print("Terminal inline image display not supported; open the PNG path above.")


