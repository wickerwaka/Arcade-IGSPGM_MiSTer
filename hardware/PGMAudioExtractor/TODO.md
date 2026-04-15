# TODO

## Remaining work

- review whether block timestamps are sufficient or whether dedicated anchor packets would improve reconstruction
- investigate the reported `stream_dropped_packets` status counter and decide whether it reflects only startup/no-host drops or an avoidable steady-state condition
- consider adding a separate debug/control channel if simultaneous human-readable logging is needed
- add optional host-side tools for splitting captures by detected rate changes or exporting alternative metadata formats
- decide whether the shared `ring_buffer` helper should stay or be simplified now that UAC is gone

## Nice to have

- richer post-capture analysis and summary tooling
- optional FLAC export on the host side
- host-to-device control commands for future capture modes
