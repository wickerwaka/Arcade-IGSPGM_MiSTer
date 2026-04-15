# PGMAudioExtractor

PGMAudioExtractor is RP2350/Pico 2 firmware for capturing the PGM arcade board's external stereo serial audio stream at its **native sample rate** and exporting it over USB CDC as timestamped binary packets.

The host-side capture tool reconstructs the stream into:
- a `.wav` file for listening
- a `.jsonl` metadata sidecar for integrity and timing analysis

This project no longer uses USB Audio Class for primary capture. The current design is aimed at **faithful native-rate extraction** rather than presenting as a standard microphone.

## Current status

Implemented:
- PIO + DMA capture from `CLK`, `LRCLK`, and `SI`
- runtime LRCLK measurement
- native-rate CDC packet transport
- packet metadata with block sequence, frame index, timestamps, and measured LRCLK
- host-side Python capture tool
- picotool-compatible reset/load workflow
- correct capture of both silence and active audio on macOS

See [TODO.md](TODO.md) for remaining cleanup and validation work.

## Audio format assumptions

Current input assumptions:
- externally clocked serial audio
- signals: `CLK`, `LRCLK`, `SI`
- stereo
- signed 16-bit PCM
- 2's complement
- MSB first
- backward-justified / I2S-like timing
- native source rates currently observed around `33074 Hz`, with `44100 Hz` also expected

## USB interface model

The firmware currently enumerates as:
- CDC serial interface carrying the binary capture stream
- reset/vendor interface for `picotool` reboot workflows

There is no UAC streaming path in the current extractor design.

## Build

```sh
make
```

Useful targets:

```sh
make                 # build UF2
make build           # configure + build
make info            # query device info
make load            # build and flash UF2
make bootsel         # reboot device into BOOTSEL mode
make app             # reboot device into application mode
make monitor         # open a serial monitor using screen
make capture         # run Python capture tool (default /tmp/pgm_capture.wav)
make clean           # remove build directory
```

Examples:

```sh
make load
make capture
make capture OUT=/tmp/pgm.wav DURATION=20
make PORT=/dev/cu.usbmodem1101 monitor
```

Defaults can be overridden:

```sh
make PICO_BOARD=pico2
make UF2=build/pgm_audio_extractor.uf2 load
make PORT=/dev/cu.usbmodem1234561 monitor
```

## Capture workflow

Typical workflow:

```sh
make load
make capture OUT=/tmp/pgm_capture.wav DURATION=10
```

This produces:
- `/tmp/pgm_capture.wav`
- `/tmp/pgm_capture.wav.jsonl`
- `/tmp/pgm_capture.wav.raw`

The `.jsonl` file contains one JSON record per received packet with header fields and decoded status payloads.

## Packet protocol

The CDC stream is framed as binary packets with a fixed header followed by a payload.

See [PROTOCOL.md](PROTOCOL.md) for the packet layout and field definitions.

## Resilience behavior

The firmware is designed to tolerate:
- no audio source connected
- no valid LRCLK edges
- no host attached to CDC
- host attached but not actively reading

When no source is present, the device continues running and emits status packets when possible.
When the host is not connected or not draining the CDC stream, packets are dropped instead of blocking capture.

## File layout

- `CMakeLists.txt` - project build setup
- `Makefile` - build, flash, and host capture helpers
- `PROTOCOL.md` - CDC packet protocol documentation
- `src/main.c` - top-level init and main loop
- `src/tusb_config.h` - TinyUSB configuration
- `src/usb_descriptors.[ch]` - USB CDC + reset descriptors
- `src/serial_audio_capture.[ch]` - PIO/DMA capture and packet emission
- `src/serial_audio_capture.pio` - PIO program source
- `src/capture_stream.[ch]` - CDC packet queueing/transport
- `src/rate_measure.[ch]` - LRCLK measurement logic
- `src/ring_buffer.[ch]` - shared stereo frame type / helper ring buffer
- `tools/capture_stream.py` - host-side capture tool
- `tools/serial_monitor.py` - reconnecting serial monitor helper
