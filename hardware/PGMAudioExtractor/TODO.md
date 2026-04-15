# TODO

## Current state

Implemented:
- RP2350 firmware captures native stereo PCM from the PGM serial audio bus using PIO + DMA
- LRCLK is measured at runtime and included with capture metadata
- Captured audio is streamed over USB CDC as binary packets instead of USB Audio Class
- Each packet includes sample data plus block sequence, frame index, timestamp, and measured rate metadata
- Host-side Python tool can receive the stream and write WAV + JSONL metadata sidecar files
- Firmware is resilient to missing source clock/data and to no host reading from CDC
- picotool reset / load workflow remains available

## Remaining work

- validate long-duration captures for packet loss / host backpressure behavior
- verify timestamp quality and decide whether block timestamps are sufficient or need extra anchor packets
- document the binary packet format in the README
- optionally add a second CDC or vendor endpoint if a separate debug/control channel is desired
- decide whether to keep or remove the older USB Audio Class codepaths from the tree
- add host-side tools for splitting captures by detected rate changes or exporting richer metadata formats
- test the extractor workflow on Linux and Windows
