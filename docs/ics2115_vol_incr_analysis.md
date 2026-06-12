# ICS2115 VMode / VIncr volume-ramp analysis

Input data: `vol_incr.csv`

Each row contains:

```text
vmode, vincr, sampled-left-audio...
```

The source sample is constant `-32768`, so the measured audio output is used as
a proxy for the hardware volume-table gain.  The analysis scripts stream the
large CSV and summarize only transitions/resets/periods.

Generated tools:

```text
audio_tests/analyze_vol_incr.py
audio_tests/plot_vol_incr.py
audio_tests/fit_vol_incr.py
```

Generated outputs:

```text
audio_tests/vol_incr.summary.csv
audio_tests/vol_incr.report.json
audio_tests/vol_incr_fit.report.json
audio_tests/vol_incr_transition_count.png
audio_tests/vol_incr_reset_count.png
audio_tests/vol_incr_reset_period_est.png
audio_tests/vol_incr_approx_index_rate.png
audio_tests/vol_incr_max_gain.png
```

Additional long capture for the slow `vmode[1:0] == 0` family:

```text
vol_incr_more_samples.csv
audio_tests/vol_incr_more_samples.summary.csv
audio_tests/vol_incr_more_samples.report.json
```

## High-level row classifications

From the 4096-row capture:

```text
wraps:                 2074
ramps_no_wrap:          866
silent_or_static_zero: 1156
```

## VMode bit observations

Working assumption: only `vmode[1:0]` is significant for the volume-envelope
rate family.  Treat differences caused by `vmode[2]` and `vmode[3]` in this
capture as phase/noise rather than separate modes.

Rationale from comparing modes that differ only in bits 2 or 3:

```text
vmode[1:0] == 0: modes 0,4,8,12
  classification and no-wrap behavior match; high-vincr transition spacing
  matches, but first transition/phase shifts.

vmode[1:0] == 1: modes 1,5,9,13
  measured periods match within about 2 samples where enough wraps exist;
  boundary rows differ when a wrap lands just inside/outside the capture.

vmode[1:0] == 2: modes 2,6,10,14
  measured period follows 65280/vincr for all four modes; mean period
  difference from flipping bits 2/3 is below 0.01 samples.

vmode[1:0] == 3: modes 3,7,11,15
  same behavior as the other odd family; measured periods match within about
  2 samples where enough wraps exist.
```

Bits 4..7 were not present in this dataset; assume they are also insignificant
until measured otherwise.

## Candidate formulas

### `vmode[1:0] == 2`

Observed equivalent modes:

```text
2, 6, 10, 14
```

These fit an extremely clean linear period:

```text
period_samples ~= 65280 / vincr
```

Equivalently:

```text
vol_acc full ramp span = 0xff00
vol_acc += vincr per output sample
```

Fit quality for rows with at least two detected wraps:

```text
mean absolute error: ~0.015 samples
max absolute error:  ~0.455 samples
```

Examples:

```text
vincr=8    period=8160
vincr=16   period=4080
vincr=64   period=1020
vincr=128  period=510
vincr=255  period=256
```

### `vmode[1:0] == 1` or `vmode[1:0] == 3`

Observed equivalent odd modes:

```text
1, 3, 5, 7, 9, 11, 13, 15
```

Both odd low-bit modes fit the same compact/floating-style VIncr rate with a 4x
larger ramp base:

```python
x = vincr & 0x7f
exp = x >> 5
mant = x & 0x1f

if exp == 0:
    step = (0x20 + mant) / 2
else:
    step = (0x20 + mant) << (exp - 1)

period_samples ~= 261120 / step
```

Fit quality for rows with at least two detected wraps:

```text
mean absolute error: ~0.3 samples
max absolute error:  ~3.9 samples
mean relative error: ~0.006%
```

Examples:

```text
vincr=128  x=0    step=16     period~=16320
vincr=160  x=32   step=32     period=8160
vincr=192  x=64   step=64     period=4080
vincr=224  x=96   step=128    period=2040
vincr=255  x=127  step=252    period~=1036.2
```

### `vmode[1:0] == 0`

Observed equivalent modes:

```text
0, 4, 8, 12
```

The original 16384-sample capture showed only a small partial ramp for high
`vincr` values.  The longer `vol_incr_more_samples.csv` capture contains 262144
samples for `vmode == 0` and confirms that only `vincr=224..255` wrap; all
`vincr <= 223` remain exactly zero/static across the whole longer window.

The refined period formula for this family is:

```python
if vincr >= 224:
    step = (vincr - 192) / 64
    period_samples = 65280 / step
                   = 65280 * 64 / (vincr - 192)
else:
    step = 0
```

Equivalently, for `vincr=224..255`:

```python
mant = vincr & 0x1f
period_samples = 4177920 / (0x20 + mant)
```

Examples from the longer capture:

```text
vincr=224  observed period=130560  predicted=130560
vincr=232  observed period=104448  predicted=104448
vincr=240  observed period=87040   predicted=87040
vincr=248  observed period=74624   predicted=74605.7
vincr=255  observed period=66304   predicted=66316.2
```

Fit quality over `vincr=224..255`:

```text
mean absolute period error: ~9.6 samples
max absolute period error:  ~23.3 samples
mean relative error:        ~0.011%
```

The exact observed periods are quantized by the finite number of wraps in the
capture, but the product `period * (vincr - 192)` clusters tightly around:

```text
65280 * 64 = 4177920
```

## HDL implementation notes

The HDL implementation should treat `REG_VMODE` (`0x12`) as a per-voice high-byte
register stored in `voice_state_t.vol_mode`.  Only `vol_mode[1:0]` selects the
rate family.

The top-level legacy `ramp_cnt`/`vol_incr[7:6]` divider should not gate envelope
updates.  The oscillator should update the envelope every processed output sample
using the per-voice step below, in internal `vol_acc` units:

```text
mode 0:
  if vincr >= 224: step = (0x20 + vincr[4:0]) << 4
  else:            step = 0

mode 1 or 3:
  if vincr == 0:   step = 0
  else if vincr[6:5] == 0: step = (0x20 + vincr[4:0]) << 7
  else if vincr[6:5] == 1: step = (0x20 + vincr[4:0]) << 8
  else if vincr[6:5] == 2: step = (0x20 + vincr[4:0]) << 9
  else:                    step = (0x20 + vincr[4:0]) << 10

mode 2:
  step = vincr << 10
```

`VOL_INCR` (`0x06`), `VOL_START` (`0x07`), `VOL_END` (`0x08`), and `VMODE`
(`0x12`) are high-byte voice-register accesses.  `VOL_START`/`VOL_END` high-byte
writes map to internal bits `[25:18]` with low fractional bits cleared.

## Next validation steps

1. Optionally capture VMode values with bits 4..7 set to validate the current
   assumption that only `vmode[1:0]` matters.
2. Compare predicted transition positions against raw sample transitions, not
   only reset periods.
3. Rerun simulator-vs-hardware audio capture comparisons with the HDL step logic.
