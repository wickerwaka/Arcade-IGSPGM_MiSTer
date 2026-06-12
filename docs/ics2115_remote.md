# ICS2115 remote hardware control

This is an interactive hardware test path for changing ICS2115 registers through PicoROM/debug-link and sampling audio through the PGMAudioExtractor/capture stream.

## TestROM page

Build and upload the new page:

```sh
make -j8 -C testroms TARGET=pgm_test PAGE=ics_remote
make -C testroms TARGET=pgm_test PAGE=ics_remote picorom
```

The page uses `testroms/debug_link.h` and forwards commands to the existing Z80 ICS driver through `z80_ics_host.c`.

## Python library

Main control API:

```py
from util.ics2115_remote import ICS2115Remote, Voice

with ICS2115Remote.open('pgm') as ics:
    print(ics.ping())

    # Register access by datasheet-ish or struct field names.
    # VMode is per-voice register 0x12 and uses the upper data port.
    ics.write_global('ACTIVE_OSC', 0x1f)
    ics.write_reg(0, 'OSC_FC', 0x0155)
    ics.write_reg(0, 'VMode', 0x00)
    print(hex(ics.read_reg(0, 'osc_fc')))

    # Full voice access with raw fields plus friendly address helpers.
    v = Voice.from_bios_trace()
    ics.write_voice(0, v)
    print(ics.read_voice(0).to_dict())
```

Simulator-native mode uses the same API, but starts `sim/sim --server` and talks to native simulator ICS2115 methods instead of PicoROM/debug-link. This avoids wall-clock serial timeouts; audio capture advances deterministic simulator cycles.

```py
from util.ics2115_remote import ICS2115Remote, Voice

with ICS2115Remote.open_sim(game="pgm_test") as ics:
    # Let the loaded program finish any startup sound-chip init before direct
    # register experiments, otherwise the running CPU/Z80 may overwrite voices.
    ics.sim.call("sim.run_cycles", {"count": 5_000_000})

    ics.play_voice(0, Voice.from_bios_trace())
    ics.open_audio()
    samples = ics.capture_audio_frames(2048)
```

Pass `transport="debug_link"` to `open_sim()` to drive the same TestROM/Z80-driver path that real hardware uses — this is the **conformance transport** for register-behavior tests (`docs/ics2115_register_matrix.md`): the same script runs against hardware (`ICS2115Remote.open`) and the simulator with the ICS2115 exercised through identical 68k/Z80 code. The native path bypasses the register interface entirely and is preferred only for audio/state introspection experiments.

In the simulator, `debug_link` data does not actually use the PicoROM ROM-mailbox byte protocol: 68k ROM reads go through `rom_cache`, which would serve stale mailbox bytes (the sim patches SDRAM behind the cache). Instead `testroms/debug_link.c` publishes a `RamComms` ring-buffer block in WORK_RAM (section `.comms_buffer`, magic `RFIF`); the simulator scans WORK_RAM for the magic on the first `debug_link.write`, sets `sim_active`, and streams request/response bytes through the rings (WORK_RAM is uncached dual-port BRAM, so simulator writes are coherent). The TestROM prefers the RAM block when `sim_active == 1` and falls back to the ROM mailbox otherwise, so the same TestROM binary works with PicoROM on hardware. Throughput is bounded by the page's once-per-vblank poll: ~1 frame of emulated time per synchronous request/response round trip; batch multiple requests per write to amortize (the page processes every complete request in a single poll).

Optional audio reader:

```py
with ICS2115Remote.open('pgm') as ics:
    ics.open_audio()          # optional serial port arg: open_audio('/dev/cu...')
    ics.play_voice(0, Voice.from_bios_trace())

    # Uses PGMAudioExtractor triggered capture mode. The firmware clears its
    # queue, discards a small number of internal DMA blocks, captures the next
    # frames, then returns to idle. No close/reopen cycle is required.
    samples = ics.capture_audio_frames(2, timeout=2.0)
    print(samples)            # [(left, right), ...]
```

The audio-only reusable module is `util/capture_audio.py`:

```py
from util.capture_audio import AudioStreamReader

with AudioStreamReader.open() as audio:
    samples = audio.capture_frames(2, timeout=2.0)
    blocks = audio.capture_blocks(4, timeout=2.0)
    audio.set_continuous()    # restore normal continuous streaming
```

## Wire protocol

Request header:

```text
'I' 'C' version seq cmd payload_len payload...
```

Response header:

```text
'i' 'c' version seq status payload_len payload...
```

All multi-byte fields are big-endian.  The Python side uses `read_exact()` for response headers and payloads.

Implemented commands:

```text
0x01 PING
0x02 INIT
0x10 READ_REG
0x11 WRITE_REG
0x20 READ_VOICE
0x21 WRITE_VOICE
0x30 GET_IRQ_COUNTS
0x31 RESET_IRQ_COUNTS
```

Responses only ACK success/error; writes do not perform automatic readback verification.
