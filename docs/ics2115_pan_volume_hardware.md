# ICS2115 hardware pan/volume measurements

`pan_vol_test.csv` records one-voice output for a constant input sample of
`-32768`.  The swept fields are:

- `pan`: `0x00..0xf0` in steps of `0x10`
- `vol_acc`: `0x0000..0xfff0` in steps of `0x10`
- `left`, `right`: captured output samples

The measurements imply that the mixer computes a 12-bit volume index from
`vol_acc >> 4`, subtracts a pan attenuation from that index, converts the result
through a 4096-entry exponential volume table, and then multiplies the sample by
that table value.

## Exact measured function

This Python code reproduces every row of `pan_vol_test.csv` exactly:

```py
import csv

PAN_ATTEN = [
    4096, 508, 364, 304,
    248, 200, 168, 140,
    116, 96, 76, 56,
    40, 28, 12, 0,
]

def volume_lut(i):
    if i <= 0:
        return 0
    if i > 4095:
        i = 4095

    exp = i >> 8
    mant = i & 0xff

    if exp == 0:
        return mant >> 7

    return (((0x100 | mant) << (exp - 1)) + 0xff) >> 8


def f(sample, pan, volume):
    vol_index = volume >> 4
    pan_index = pan >> 4

    left_atten = PAN_ATTEN[15 - pan_index]
    right_atten = PAN_ATTEN[pan_index]

    left_gain = volume_lut(vol_index - left_atten)
    right_gain = volume_lut(vol_index - right_atten)

    left = (sample * left_gain) >> 15
    right = (sample * right_gain) >> 15
    return left, right


bad = []
with open('pan_vol_test.csv', newline='') as fh:
    for row in csv.DictReader(fh):
        pan = int(row['pan'])
        volume = int(row['vol_acc'])
        expected = (int(row['left']), int(row['right']))
        got = f(-32768, pan, volume)
        if got != expected:
            bad.append((pan, volume, expected, got))

print(len(bad))  # 0
```

For `sample == -32768`, the final multiply simplifies to `-gain` because:

```py
(-32768 * gain) >> 15 == -gain
```

## Volume table

The measured table is close to the old MAME/HDL patent-derived table, but it
rounds upward for non-zero fractional entries.

Measured table:

```py
def volume_lut(i):
    if i <= 0:
        return 0
    exp = i >> 8
    mant = i & 0xff
    if exp == 0:
        return mant >> 7
    return (((0x100 | mant) << (exp - 1)) + 0xff) >> 8
```

Equivalent interpretation for `exp > 0`:

```py
ceil(((0x100 | mant) << exp) / 512)
```

The `exp == 0` case must be special-cased; otherwise `exp - 1` is `-1`, which
would be an invalid negative shift in Python and undefined/impossible in C/HDL.

Examples:

```text
index  old MAME/HDL  measured
128    0             1
255    0             1
256    1             1
257    1             2
511    1             2
4095   32704         32704
```

## Pan table

The old MAME/HDL table used:

```text
panlaw[0] = 0xfff
panlaw[i] = 16 - floor(log2(i))
```

The hardware measurements instead match a 16-step attenuation table indexed by
`pan >> 4`:

```py
PAN_ATTEN = [
    4096, 508, 364, 304,
    248, 200, 168, 140,
    116, 96, 76, 56,
    40, 28, 12, 0,
]
```

The HDL stores attenuation in 12 bits, so the first entry is represented as
`0xfff`.  For the measured 12-bit volume index range, `4096` and `4095` both
fully mute because the post-pan index is `<= 0`.

Stereo uses the table symmetrically:

```py
left_atten = PAN_ATTEN[15 - (pan >> 4)]
right_atten = PAN_ATTEN[pan >> 4]
```

Only pan values in steps of `0x10` were measured in this CSV.  The HDL and MAME
now map all values with the same high nibble to the same attenuation, matching
what the measured function does for the swept values.
