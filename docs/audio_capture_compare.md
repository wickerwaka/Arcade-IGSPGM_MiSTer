# Audio capture comparison tool

`util/compare_audio_captures.py` compares two WAV captures that contain one
sound effect surrounded by silence.  Sample-rate metadata is ignored; the WAV is
treated only as a list of PCM samples.

## Usage

```sh
uv run python util/compare_audio_captures.py REF.wav TEST.wav
```

Paths may be full paths or names under `audio_captures/`:

```sh
uv run python util/compare_audio_captures.py \
  sim_20260517_2055_wave_02.wav \
  sim_20260517_2059_wave_02.wav \
  --json /tmp/compare.json \
  --plot /tmp/compare.png
```

## Method

1. Read PCM WAV samples.  No resampling is done.
2. Find the first and last non-zero frame in each capture.  A frame is active if
   any channel has `abs(sample) > threshold`.
3. Align the two captures by their first active frame.
4. Compare the overlapping active regions.
5. Report metrics and categories.

Optional arguments:

- `--threshold N`: treat samples with `abs(sample) <= N` as silence.
- `--max-lag N`: diagnostic cross-correlation lag search around the first-active
  alignment.  This does not resample or time-scale the data.
- `--json PATH`: write the full report as JSON.
- `--plot PATH`: write a small dependency-free PNG overlay/difference plot.

## Categories

The first version categorizes common differences:

- `reference_silent`
- `test_silent`
- `length_mismatch`
- `possible_residual_timing_shift`
- `polarity_inverted`
- `amplitude_gain_mismatch`
- `minor_amplitude_gain_difference`
- `channel_balance_mismatch`
- `large_waveform_difference`
- `moderate_waveform_difference`
- `small_waveform_difference`
- `close_match`
- `dc_offset_difference`

The goal is not bit-exact pass/fail.  The metrics are intended to help classify
how two captures differ: gain, channel balance, residual noise/waveform error,
length/tail differences, and possible remaining alignment offset.
