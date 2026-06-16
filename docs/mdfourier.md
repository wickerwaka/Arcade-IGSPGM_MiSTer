# MDFourier test signal for the PGM ICS2115

This is a device-generated [MDFourier](https://junkerhq.net/MDFourier/) signal
for measuring end-to-end ICS2115 audio fidelity of the core against real
hardware, and for regressing it over time. The same signal is played on both
targets, captured to WAV, and compared.

## Prerequisites (read this first)

Every host-driven ICS test here — `run.py`, `sync_test.py`, and anything else
using `ICS2115Remote` over the debug link — talks to the **`ics_remote` page** of
the `pgm_test` ROM. Two things must be set up or the tools fail in confusing ways:

1. **Build the test ROM with `PAGE=ics_remote`.** The default `pgm_test` build
   (`make -C testroms TARGET=pgm_test`, no `PAGE=`) compiles only `sprite_test` +
   `debug` — it does **not** include `ics_remote`, so the debug-link mailbox is
   never serviced and every command (even `ping`) times out as:

   ```
   ICSRemoteProtocolError: short read: wanted 6, got 0
   ```

   If you see that, you almost certainly forgot `PAGE=ics_remote`:

   ```bash
   make -j8 -C testroms TARGET=pgm_test PAGE=ics_remote
   ```

   `ics_remote` is the only page in that build, so the ROM boots straight into it
   and the debug link is live immediately (`ping` returns `driver_magic=0x1c53`).
   (The standalone `mdfourier` page is the exception — it self-runs on boot and
   needs no host; build it with `PAGE=mdfourier`.)

2. **Point the simulator at the correct ROM set** with `PGM_ROM_DIR`. The sim
   defaults to `../roms`, which does not exist in this checkout. Use:

   ```bash
   PGM_ROM_DIR=~/Documents/PGM_Roms python3 util/mdfourier/run.py --target sim ...
   ```

   Do **not** use `~/Source/PGMBuilder/roms` — it is the wrong set.

## Why it is reproducible

The whole signal is driven by **ICS timer 0** from inside the Z80 timer-IRQ
handler (`testroms/z80_ics_driver.c`, `service_irq_c` → `mdf_tick`). The ICS
timer and the audio engine share the RTL clock, so every block/element boundary
lands at the same output-sample offset on hardware and in the simulator. The two
captures therefore differ **only** by ICS2115 audio fidelity — exactly what
MDFourier measures. Only the Z80 can reach the ICS registers (they sit on the
Z80 I/O bus), which is why the sequencer lives on the Z80.

## Signal structure

Defined once in `util/mdfourier/mdf_signal.py` (the single source of truth) and
derived from there into the Z80 script, the `.mfn` profile, and the capture
length:

- **Start sync** — an 8820 Hz pulse train (MDFourier alignment marker).
- **Silence** — noise floor.
- **Tones** — a dense rising semitone sweep (72 tones, 6 octaves).
- **Pan sweep** — a fixed mid tone stepped through 16 L→R pan positions
  (reg 0x0c), analyzed in stereo.
- **Silence**, then an **end sync** pulse train.

Tones come from looping a fixed 64-byte region of the BIOS music ROM and varying
`osc_fc`; the loop repeats at `f = osc_fc·Fs / (1024·loop_len)`, so pitch is
proportional to `osc_fc` and one semitone is a factor of `2**(1/12)`. The timbre
is harmonic-rich, not a pure sine — correct for MDFourier, which analyzes real
device output.

Sample rate is the native **33075 Hz** (= 33.8688 MHz / 1024); MDFourier accepts
arbitrary rates and clamps analysis to Nyquist, and the ICS output has nothing
above ~16.5 kHz, so no resampling is done by default.

The timer-0 period is set in `mdf_signal.py` (one IRQ per MDFourier "frame"); the
profile's frame-ms is computed from it.

## Sequencer (ping-pong, jitter-minimized)

The Z80 drives the sequence from the continuously-running ICS timer-0 IRQ. To
keep each sample's start time consistent (low jitter relative to the steady timer
edge), it **ping-pongs two voices**: while one voice sounds the current entry,
the next entry is pre-staged (`osc_conf`/`osc_fc`/pan written, voice left
stopped) on the other voice during the hold. At each tick the only time-critical
work is the key-on of the pre-staged voice (plus key-off of the previous), then
the following entry is pre-staged after the trigger. The Z80 also `HALT`s waiting
for the timer IRQ during playback, so every tick is serviced from the identical
state → constant IRQ latency. The host loads the shared voice template (loop
region + full volume, stopped) onto **both** MDF voices (0 and 1) before start.

Inspect the current signal with:

```bash
python3 util/mdfourier/mdf_signal.py
```

## Files

- `util/mdfourier/mdf_signal.py` — signal definition / single source of truth.
- `util/mdfourier/make_profile.py` — writes `pgm_mdfourier.mfn` (and optionally
  the C script header for the standalone page).
- `util/mdfourier/run.py` — play + capture a WAV from sim or hardware.
- `util/mdfourier/sync_test.py` — focused sync-pulse timing test (see below).
- `util/mdfourier/compare.py` — timer-locked spectral compare + MDFourier hand-off.
- `util/mdfourier/pgm_mdfourier.mfn` — generated MDFourier profile.
- `testroms/pages/mdfourier.c` + `mdfourier_script.h` — standalone capture page.
- Driver/protocol: `testroms/z80_ics_driver.c`, `z80_ics_protocol.h`,
  `z80_ics_host.c`, `ics_remote_protocol.h`, `pages/ics_remote.c`.
- Python wrappers: `ICS2115Remote.mdf_load/mdf_start/mdf_status` in
  `util/ics2115_remote.py`.

## Usage

Regenerate the profile (and the standalone-page script header) after changing
the signal:

```bash
python3 util/mdfourier/make_profile.py --c-header
```

Build and run. The capture is driven over the TestROM debug-link path, so build
the test ROM with the `ics_remote` page:

```bash
make -C sim sim
make -j8 -C testroms TARGET=pgm_test PAGE=ics_remote
```

Capture from the simulator (use `--short` for a fast smoke test) and from
hardware:

```bash
python3 util/mdfourier/run.py --target sim --out /tmp/mdf_sim.wav
python3 util/mdfourier/run.py --target hw  --out /tmp/mdf_hw.wav
```

Compare:

```bash
python3 util/mdfourier/compare.py --ref /tmp/mdf_sim.wav --comp /tmp/mdf_hw.wav
# add --mdfourier /path/to/mdfourier to also run the full Fourier analysis
```

`compare.py` verifies the captures are sample-aligned (lag ≈ 0), then prints a
per-element peak-frequency/magnitude table so any fidelity gap is localized to a
specific tone or pan position. It also prints the exact `mdfourier` command line
(`-P` profile, `-r` reference, `-c` comparison).

The standalone `mdfourier` page plays the signal once on boot without a host
(useful for capturing on real hardware via the audio extractor):

```bash
make -C testroms picorom PAGE=mdfourier
```

## Focused sync-pulse timing test

MDFourier alignment depends on the sync pulse train being a clean square wave in
time: each pulse exactly one frame long, each gap one frame. `sync_test.py`
isolates **just** the sync pulses (no tones/pan), captures them, and
measures every pulse's ON duration and following OFF gap via a sub-sample
envelope edge detector, reporting bias, spread, and PASS/FAIL at a ±0.1 ms
tolerance. Same `PAGE=ics_remote` + `PGM_ROM_DIR` prerequisites as above.

```bash
PGM_ROM_DIR=~/Documents/PGM_Roms \
  python3 util/mdfourier/sync_test.py --target sim --pulses 20 --out /tmp/sync_sim.wav
PGM_ROM_DIR=~/Documents/PGM_Roms \
  python3 util/mdfourier/sync_test.py --target hw  --pulses 30
```

## Notes

- `--short` shrinks the signal (fewer tones / shorter holds) for fast simulator
  iteration; pass it to `run.py`, `make_profile.py`, and `compare.py` together so
  the profile and analysis match the capture.
- `--resample <rate>` on `run.py` is available but off by default; if used, run
  it identically on both targets so it cancels out.
