# TODO

## MVP status

Completed:
- USB Audio Class capture device enumerates correctly
- CDC debug console and picotool reset interface work
- LRCLK runtime measurement is implemented and stable
- PIO + DMA serial audio capture from `CLK`, `LRCLK`, and `SI` is implemented
- Backward-justified capture is working for the current PGM source
- Real silence and real program audio have been captured successfully
- USB stream close/reopen is stable
- Default USB sample rate now tracks the measured source rate (`33074` by default, `44100` when detected)

## Remaining work

- reduce or eliminate ring buffer frame drops under sustained capture
- trim debug instrumentation/heartbeat logging once no longer needed
- validate capture behavior with additional hosts/applications
  - macOS Audacity
  - Linux
  - Windows
- test more source material/games and confirm channel ordering/polarity across titles
- consider exposing cleaner rate-switch behavior if more source rates are encountered
- clean up build helper artifacts and document the recommended capture workflow
