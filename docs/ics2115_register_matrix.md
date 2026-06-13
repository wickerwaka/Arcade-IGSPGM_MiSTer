# ICS2115 Register Behavior Matrix

Tracking document for validating the ICS2115 RTL (`rtl/ics2115/`) against real
hardware. Every known register access from firmware evidence gets one or more
**behavior rows**; each row carries a status and a test ID. The goal is to burn
this document down until every row is `TESTED-PASS` or justified `N/A`, with a
test suite that produces identical output on the simulator and real hardware.

Evidence corpora, in order of authority:

1. **PGM Z80 BIOS** decompile — `~/Source/PGM-BIOS/pgm_bios_z80_raw_decompiled.c`
   (access sites enumerated in [`ics2115_access_inventory.csv`](ics2115_access_inventory.csv),
   regenerate/verify with `python3 util/ics_access_inventory.py [--check]`).
2. **Turtle Beach WaveFront firmware** (`OSWF.bin`) — analyzed in
   `~/Source/PGM-BIOS/ICS2115_OSCILLATOR_REGISTERS.md` (cited below as **TB doc**).
3. **Hardware experiments** — prior captures behind
   `docs/ics2115_pan_volume_hardware.md` and `docs/ics2115_vol_incr_analysis.md`.
4. **MAME** `~/Source/mame/src/devices/sound/ics2115.cpp` — reference only, has
   open TODOs ("Verify interrupt, envelope, timer period / Verify unemulated
   registers", ics2115.cpp:14-15).
5. **GF1 netlist** `~/Source/ICS2115/LPC-GUS/gf1.v` — microarchitecture hints
   from a related chip (timers ~440-487, voice RAM ~73-300); never authority.

## Status legend

```
UNKNOWN       no evidence for what real hardware does
ASSUMED       behavior inferred from firmware usage / references; not modeled or untested
MODELED       RTL implements the assumed behavior; no register-level validation test yet
TEST-DEFINED  test exists, not yet run on both targets
TESTED-SIM    test passes on simulator; hardware run pending
TESTED-PASS   sim and hardware match (and match the assumed behavior)
TESTED-FAIL   sim/hardware diverge, or hardware contradicts the assumption -> RTL fix needed
N/A           unreachable / don't-care (justification required)
```

Burn-down dashboard (run from repo root):

```bash
grep -oE '\| (UNKNOWN|ASSUMED|MODELED|TEST-DEFINED|TESTED-SIM|TESTED-PASS|TESTED-FAIL|N/A) \|' \
    docs/ics2115_register_matrix.md | sort | uniq -c
```

## Test ID index

Tests live in `util/ics_reg_tests/` (T-RB and T-PROBE implemented; runner
`util/run_ics_reg_tests.py --target {sim,hw}`, differ
`util/compare_ics_results.py`). All run through the Z80 driver path on both
targets (`ICS2115Remote` hardware / `open_sim(transport="debug_link")`),
emitting JSONL for a common differ.
**Sim-native (`SimICS2115Remote`) is introspection-only** — it bypasses the
register interface and returns zeroed IRQ counts.

Most rows are **record-only**: the test records what the chip does rather than
asserting an expectation, and adjudication happens when the hardware JSONL is
diffed against the sim baseline. For those rows `TESTED-SIM` means the sim
baseline is captured and deterministic (two consecutive runs diff clean —
verified 2026-06-11, 152/152 records, `results/sim_run1.jsonl` is the baseline
artifact).

The sim debug-link transport is validated (2026-06-11,
`audio_tests/debug_link_bench.py`): ~1 vblank frame of emulated time per
synchronous command (~0.6 s wall; `read_voice` 2 frames), 200/200 mixed
read/write ops clean. In the simulator the byte stream rides a WORK_RAM ring
(`RamComms`, see `docs/ics2115_remote.md`) because `rom_cache` makes the
PicoROM ROM-mailbox incoherent there; the TestROM auto-detects and the 68k/Z80
register path is identical on both targets. Batch requests to amortize the
per-poll frame cost in large sweeps.

| ID | What it does | Infra needed |
|---|---|---|
| T-RB | per-register readback: walking bits, voice-select isolation, byte-width disturbance (does a LOWER8 write touch the upper byte?) | existing protocol |
| T-PROBE | reserved regs 0x13-0x3F, 0x44-0x49, 0x4E: read defaults, write-then-read, full 0x40-0x4F snapshot before/after to catch aliasing, IRQ-count + audio side-effect check | existing protocol |
| T-TMR | timer period vs `((scale&0x1f)+1)*(preset+1) << (4+(scale>>5))`: (preset, scale) grid incl. 0x00/0x1f/shift extremes, measured as IRQs per N vblank frames | I-1 |
| T-IRQ | 0x4A enable-vs-pending, 0x43 read status bits, clear-on-read of 0x40/0x41 (which port widths trigger it?), IRQ line gating with enable=0 | I-1, I-2 |
| T-IRQV | multi-source IRQV ordering/priority, auto-clear granularity (one source or both? low-byte vs high-byte read?), ActiveOsc gating of the scan, idle read value | I-2 |
| T-VOICE | one-shot/loop/bidir end IRQs, vol-ramp IRQ at VEnd, IRQ from a voice above ActiveOsc | I-2 |
| T-OCTL | OscCtl key-on variants 0x10/0x20/0x30 (audio effect?), `shadow|0x02` transient, readback width/value, MAME's timer-start-bit guess (write bit0/1 with timers configured but stopped, watch IRQ counts) | I-1 |
| T-VMODE | 0x12 per-voice vs global scope (write voice A, read voice B), bits 7:2 readback + audio effect | audio capture |
| T-SYS | 0x4D readback + bit-gating side effects + strobe replay; 0x4C write sensitivity; 0x4B vector semantics; reg-select port echo; port-0 status bits | I-1, audio capture |
| T-AUD | OscConf format-bit encoding sweep (TB doc conclusion 4), regression of pan/vol tables | existing capture/compare |

Infra items: **I-1** = frame-counted IRQ measurement (TestROM vblank frame
counter + atomic `{frame_count, irq_counts}` remote command) and **I-2** =
Z80-side IRQ event ring (raw status/IRQV/0x43 bytes per service, 32 entries) —
both implemented 2026-06-11 (driver magic 0x1c52) and validated: measured
timer rate 35 IRQs/20 frames vs 34.9 predicted at 103 Hz.

**Convergence status (2026-06-12)**: after the RTL fix package, 172/178
records match between targets (`results/sim_baseline.jsonl` vs
`results/hw_baseline.jsonl`). The 6 residuals: hardware run-to-run variance
in IRQV round-robin corner cases (multi-pend ordering and ActiveOsc gating
need tests that precondition the round-robin pointer deterministically),
the 0x4D initial-read bit3 quirk, VCtl/OscConf 0xFF-pattern readback
variance (engine-state-dependent), and the latched-delivery stat43 nuance.
PGM BIOS boots in sim with audio on the new RTL.

**Regression postmortem (kov2 stuck-chord, fixed 2026-06-12)**: replacing the
VCtl readback stub with true stored-byte readback exposed that the envelope
engine never evaluated collapsed/zero-step ramp boundaries (row 0D-D) — the
BIOS teardown's DONE poll hung the Z80 inside its IRQ handler. Two lessons:
(a) a long-lived stub can be load-bearing for firmware code paths no register
test exercises — game-level audio runs (boot sweeps + FPGA-captured
savestates) are a required layer of the validation loop; (b) voice pends are
event-latches driven by engine EVENT PULSES — deriving them from stored-bit
echoes creates consume/write-back races (perpetual retrigger).

Timer measurement constraints (measured in sim): timer rates above ~2 kHz
IRQ-storm the Z80 and starve its command loop entirely; above ~830 Hz the
Z80 service loop saturates and counts read exactly half (fires merge into the
still-set pending bit). The ceiling differs between targets (ICS port wait
states), so T-TMR grid points stay <= ~420 Hz. T-TMR/T-IRQ/T-IRQV sim
baseline: `results/sim_irq_run2.jsonl` (determinism verified 20/20 vs run3).

---

# Hardware findings — global conventions (2026-06-11, first hardware sessions)

Cross-cutting discoveries from the first real-hardware suite run and the
follow-up characterization sessions (full per-register details in the
sections below; raw data `results/hw_run1.jsonl` + session transcripts):

- **Unimplemented bits and byte lanes read as 1** (pulled high), not 0:
  unused low lanes of Upper8 registers read 0xFF; pan low nibble, VMode upper
  nibble, OscAcc-low bits 2:0, OscFC bit 0 all read as ones. The RTL/MAME
  convention of returning 0 for unimplemented bits is wrong almost
  everywhere. Exception: OscSAddr's other lane reads 0x3F (bits 7:6 driven
  low — saddr may be wider than 8 bits).
- **16-bit sample playback is byte-duplicated**: the PGM board wires the
  8-bit music-ROM data bus so lin16 mode reads the addressed byte into both
  halves of the 16-bit sample. True 16-bit playback is impossible on this
  board; the engine models the duplication.
- **Several registers store fewer bits than modeled**: pan = 4 bits (upper
  nibble; matches the 16-step pan table), VMode = 4 bits (rate[1:0] +
  phase[3:2], per-voice), OscCtl = 2 bits (RTL already correct).
- **Timer architecture is asymmetric** (see 0x42/0x43): timer 0 = mult+shift
  from 0x42; timer 1 = shift-only from 0x43[7:5]; IRQ output enables are
  0x43 bits 3 (t0) and 4 (t1); 0x4A is uninvolved; 0x4D bit 0 gates timer
  counting entirely.
- **Voice IRQ model (T-VOICE, real oscillator events)**: per-voice enable =
  OscConf bit5 (osc) / VCtl bit5 (vol); global output gate = **0x4A** (with
  0x4A=0 the pend latches silently; with 1 the Z80 is interrupted). The INT
  level follows (source condition AND enables) — clearing conf bit7 alone
  re-asserts immediately (measured 12k storm services); the ack is clearing
  the per-voice enable / tearing the voice down, exactly what the BIOS
  handler does. IRQV is NOT clear-on-read. 0x4B reads 0x80|voice while an
  osc IRQ pends, and its voice field even tracks non-pending end events
  (valid bit clear). Earlier "host pends never assert" observations were
  made with 0x4A=0.
- **IRQV idle value retains the last reported voice** in bits 4:0 (e.g. 0xE2
  after voice 2), unlike the RTL's 0xFF; the latch even survives a board
  reset (ICS state is not cleared by the 68k reset line).
- **Register selects 0x50-0x57 hit real registers** (distinct lane-structured
  values: 0x7474, 0x4d4d, 0x7d7d, 0x8888, 0x20a0, 0x5050); 0x58+ reads
  constant 0x7878 (open bus). Unprobed beyond reads.
- **Hardware Z80 IRQ service ceiling is >1033 Hz** (vs ~830 Hz in the
  simulator) — T-TMR grid points stay <= ~420 Hz to be exact on both.
- The suspected "stale-read wedge" in the first hardware runs was NOT a
  wedge: every reading is explained by (a) timers unarmed without the 0x43
  enable bits, and (b) host voice-pends requiring OscConf bit7 AND bit5
  together (0x80 alone latches nothing, so IRQV/0x4B/conf reads showed
  idle values). Full-sequence bisects with canaries confirm the read paths
  are robust. The one REAL hazard: writes to reserved voice registers —
  0xFFFF to 0x13 hung the ICS host interface (BUSY stuck) in one session,
  state-dependent; write probing now lives in the separate t_probe_w group,
  excluded from hardware baselines.
- Timer-0 mult field 0 (scale[4:0]=0) stops timer 0 entirely on hardware,
  like preset=0.

# Direct host ports (Z80 0x8000-0x8003)

These are not indexed registers but the four bus ports; the BIOS proves
behaviors here that predate any register access.

## Port 0x8000 — status (read)

RTL: `ics2115.sv:735-754`. bit7 = any enabled IRQ active, bit1 = any voice IRQ
pending, bit0 = timer IRQ pending&enabled, bit6 = **RTL-specific** "buffered
voice write FIFO busy".

Evidence: PGM drivers dispatch on bit1 -> IRQV drain loop (comment at
`ics2115.sv:687-690`); BIOS helper `ReadIrqStatusPort` (ram:02a3) exists but has
**no caller in the decompiled BIOS** — the dispatch evidence comes from game
drivers (theglad/espgaluda).

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| P0-A | bit7 = master IRQ flag, bit1 = voice IRQ, bit0 = timer IRQ | T-SYS | TESTED-PASS |
| P0-B | bit1 polarity/gating: raw pending vs enable-gated (RTL gates the whole byte under `irq_on` but bit1 itself is raw pending) | T-IRQ | TESTED-PASS |
| P0-C | bit6 read value on real hardware (RTL uses it for its write-FIFO; real chip may define it differently or not at all) | T-SYS | TESTED-PASS |

## Port 0x8001 — register select (write + **readback**)

RTL: echo modeled at `ics2115.sv:749`.

Evidence: `ProbeSoundChipRevision` (ram:16f2) writes 0x5a, 0xa5, 0x4c to this
port and **reads each back expecting the written value** before reading the
revision from the data port. Chip presence detection depends on this echo.

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| P1-A | reading port 1 returns the last written register index, for arbitrary values incl. non-existent registers (0x5a, 0xa5) | T-SYS | MODELED |

## Ports 0x8002/0x8003 — data low / data high

RTL: `ics2115.sv:750-751` (read), write decode `ics2115.sv:~1104-1180`.
Conventions (CLAUDE.md, confirmed by BIOS): word writes are low byte then high
byte; regs 0x06/0x07/0x08/0x12 are Upper8 accesses; 0x0e/0x0f are global —
never precede with a voice select.

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| P2-A | low-then-high write ordering forms a 16-bit write; what does a high-only or low-only write do to the other byte per register? | T-RB | TESTED-PASS |
| P2-B | read side-effects (IRQV clear, timer IRQ clear) trigger on the correct port: RTL clears IRQV only on **high-byte** read (`ics2115.sv:707`), timer IRQ on **either** byte (`ics2115.sv:727`) | T-IRQ, T-IRQV | ASSUMED |

---

# Per-voice registers (0x00-0x12, selected via 0x4F)

RTL read mux `ics2115.sv:570-655`; voice writes are buffered through a FIFO
(`ics2115.sv:~1104-1145`) and applied by `apply_voice_reg_byte`
(`ics2115.sv:758+`); envelope/oscillator engine in `ics2115_osc.sv`.

## 0x00 — OscConf (oscillator configuration) — Upper8

Bits (TB doc + PGM, corroborating): 7=IRQ pending/status, 6=invert/reverse,
5=IRQ enable, 4=bidir, 3=loop, 1:0=sample format. **Format encoding (TB doc
conclusion 4): `00`=linear 8-bit, `01`=u-law 8-bit, `10`=linear 16-bit,
`11`=white noise/special — diverges from MAME's bit2=8bit/bit1=stop model.**

Evidence: 6 BIOS sites (inventory). Key ones: `InitializeSoundChannels` writes
0x00 after key-off; init self-test polls OscConf until bit7 clears
(ram:17a6 loop), then forces `OscConf=0xa0` (pending+enable) to provoke the
0x4B vector check; IRQ handler reads it during voice teardown (L1330). PGM sets
bit5 for one-shot completion IRQs (TB doc conclusion 7).

RTL: read `ics2115.sv:573-575`; flags decoded per the firmware model
(`OSC_IRQ_PEND(7), INVERT(6), IRQ_EN(5), BIDIR(4), LOOP(3), 16BIT(1), ULAW(0)`);
osc-end IRQ sets bit7 in `ics2115_osc.sv:479-482`.

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 00-A | readback returns written value; bit7 set by hardware on osc IRQ | T-RB, T-VOICE | TESTED-SIM |
| 00-B | format bits 1:0 per TB encoding incl. `11` = white noise/special | T-AUD | TESTED-PASS |
| 00-C | hardware: host pend requires bit7 AND bit5 together (0xA0); bit7 alone is a no-op (RTL pends on bit7 alone). Pend populates 0x4B (0x80|voice) and IRQV but never asserts the IRQ output | T-IRQV | TESTED-PASS |
| 00-D | bit5=0 with pending bit7=1: is the IRQ line masked but the bit retained? | T-VOICE | TESTED-PASS |
| 00-F | fmt `11` = oscillator-clocked 8-bit noise generator (T-NOISE hw RE). RTL IMPLEMENTED (ics2115_osc.sv): free-running 16-bit Galois LFSR (taps 0xB400, seed 0xACE1, never reset on key-on), 8-bit output placed lin8-style, advanced one step per sample-index (acc>>12) crossing so pitch tracks OscFC. Sim reproduces all hw signatures: ROM-independent amplitude (flat across regions vs lin8 varying), 8-bit (256 distinct), OscFC scaling (zcr 0.06->0.25->0.51 vs hw 0.11->0.37->0.50 — converges at high fc; low/mid offset is resampler difference, not a model error). Exact hw polynomial unrecoverable/free choice; no PGM game uses fmt3 | T-NOISE | TESTED-PASS |

## 0x01 — OscFC (frequency counter) — WORD

Evidence: 2 BIOS sites, always `WriteICSRegisterWord(1, ...)` from pitch tables.
RTL: `ics2115.sv:578` read; low-byte write forces bit0=0 (`ics2115.sv:~791`),
step uses fc[15:1].

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 01-A | 16-bit r/w; bit0 ignored/forced 0 (hardware-trace confirmed per docs/ics2115_z80_trace_compare.md) | T-RB | TESTED-PASS |

## 0x02/0x03 — OscStart high/low; 0x04/0x05 — OscEnd high/low

Evidence: 1 BIOS site each (`ProgramSoundChannelRegisters`, WORD writes).
RTL: reads `ics2115.sv:581-590` — note **low-register reads return the low
8 bits in the high byte with 0x00 low** (`{start[12:5], 8'h00}`), and low-reg
low-byte writes are ignored.

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 02-A | 20.9 fixed-point address split across reg pairs; readback layout as modeled (incl. the `xx00` low-reg read shape) | T-RB | TESTED-PASS |
| 02-B | bits [4:0] of the low registers: RTL models them as nonexistent (write ignored, read 0) — hardware may store more precision | T-RB | TESTED-PASS |

## 0x06 — VolIncr — Upper8

Evidence: **never written by the PGM BIOS** (inventory: zero 0x06 rows — the
BIOS uses fixed VStart/VEnd=1 instead); Turtle Beach writes it during ramp
programming paired with 0x12 (TB doc). Hardware sweep already done:
`docs/ics2115_vol_incr_analysis.md` derived the rate families from real
captures (`vol_incr.csv`).

RTL: read `ics2115.sv:593`; rate families implemented in
`ics2115_osc.sv:113-149` (`calc_vol_step`).

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 06-A | ramp rate law DERIVED + RTL reworked (T-VINCR2 dense sweep): mode2 linear (incr<<10), mode0/1/3 exponential step=2^(E/32), E=incr+offset(0 for mode0, 256 for mode1/3). New calc_vol_step fits hw within ~6% / 37/42 measurable cells; remaining 5 are hw fast-cell overshoot artifacts | T-VINCR2 | TESTED-PASS |
| 06-B | readback returns written value (Upper8) | T-RB | TESTED-PASS |

## 0x07 — VolStart, 0x08 — VolEnd — Upper8

Evidence: BIOS writes both = 1 during voice init (L853-854). Hardware trace
compare confirmed these are **high-byte** accesses (docs/ics2115_z80_trace_compare.md).
RTL: read `ics2115.sv:596-599`; write sets bits 25:18, clears 17:0.

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 07-A | Upper8 write maps to envelope bits 25:18; low bits cleared on write | T-RB | TESTED-PASS |
| 07-B | readback shape (RTL returns `{bits25:18, 8'h00}`) | T-RB | TESTED-PASS |

## 0x09 — VolAcc (volume accumulator) — WORD

Evidence: 2 BIOS sites — **both write 0x09 with values from
`z80_wave_frequency_table`** (L950, L1044; the table name is the decompiler's,
indexed by a per-note byte — this is the BIOS's volume law, not a frequency).
Hardware pan/vol sweep (`vol_pan.csv`, `docs/ics2115_pan_volume_hardware.md`)
mapped vol_acc -> amplitude exactly (exponential with ceil rounding,
`ics2115_tables.sv:34-39`).

RTL: read `ics2115.sv:602` (bits 25:10); low-byte write zeroes bits 9:0.

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 09-A | vol_acc -> output amplitude curve (measured on hardware) | T-AUD regression | TESTED-PASS |
| 09-B | 16-bit readback of bits 25:10; write low byte clears 9:0 | T-RB | TESTED-PASS |

## 0x0A/0x0B — OscAcc high/low — WORD

Evidence: WORD writes in voice programming (L929-937); Turtle Beach manually
sets/increments these for the accumulator-monitor readback path (TB doc,
`StartAccumulatorMonitorRead`/`ReadAccumulatorMonitorByte`).
RTL: reads `ics2115.sv:605-610` (0x0B returns `{acc[12:0],3'b000}`, MAME-shape
mask 0xFFF8); 0x0B low-byte write takes data[7:3] -> acc[4:0].

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 0A-A | r/w current sample position; readback while running returns advancing values | T-RB | MODELED |
| 0A-B | bottom-3-bit truncation shape on 0x0B read/write | T-RB | TESTED-PASS |

## 0x0C — Pan — Upper8

Evidence: 1 BIOS site (`z80_voice_pan_table` lookup). Hardware sweep mapped the
16-step pan attenuation table exactly (`docs/ics2115_pan_volume_hardware.md`,
table in `ics2115_tables.sv:53-72`).

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 0C-A | pan -> L/R attenuation per measured 16-step table (`pan >> 4` index) | T-AUD regression | TESTED-PASS |
| 0C-B | readback returns written value | T-RB | TESTED-PASS |

## 0x0D — VCtl (volume envelope control) — Upper8

Bits (TB doc): 7=IRQ pending, 6=invert, 5=IRQ enable, 4=bidir, 3=loop,
2=rollover, 1=stop, 0=done.

Evidence: 6 BIOS sites. `InitializeSoundChannels`: read 0x0D, write back
(transient), poll until done after `OscCtl=0x0f` key-off, then write 0x03
(done+stop) before key-on setup; IRQ handler reads 0x0D and tests **bit 3**
(L1307: `& 8`) to decide voice teardown. Known hardware quirk: writing 0x00
reads back 0x01 — envelope idles in DONE (docs/ics2115_z80_trace_compare.md).

RTL: read is an **explicit stub** (`ics2115.sv:615-621`, "stub for T02 IRQ
work"): returns 0x81/0x01 depending on vmode[1:0] and the IRQ-enable flag —
it does NOT return the stored vol_ctrl bits (no bit3/loop, no real bit1/0
state). Write `ics2115.sv:~796` stores all 8 bits; vol-end IRQ sets bit7
(`ics2115_osc.sv:535-538`).

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 0D-A | readback = full stored byte mutated live by the engine (bit7 stored from writes but never latches a pend; DONE/rollover set/cleared by the engine). The old RTL stub always returned DONE=1 — load-bearing: it masked 0D-D | T-RB | TESTED-PASS |
| 0D-B | write 0x00 -> reads back 0x01 (DONE set when idle; hardware-trace confirmed) | T-RB | TESTED-PASS |
| 0D-C | bit7 set by hardware at ramp end when bit5 enabled | T-VOICE | MODELED |
| 0D-D | **the envelope evaluates its boundary every tick even with vincr=0**: the BIOS voice teardown collapses the window (VolStart=VolEnd=1, key-off) and polls VCtl until DONE sets — with zero increment. An engine that skips zero-step ramps hangs the Z80 inside the voice-IRQ handler (the kov2 stuck-chord regression, root-caused from FPGA savestates 2026-06-12). Engine also clears the rollover flag (bit2) at the boundary | game-level (kov2 audio_start/bad_audio states) | TESTED-PASS |

## 0x0E — ActiveOsc — Upper8, global

Evidence: BIOS writes 0x1f twice during init (L1551, L1614); never reduced.
MAME: sample rate = `clock / ((active+1)*32)` with "(Guessing)" on the 5-bit
mask (ics2115.cpp:150-152). PGM always runs all 32 voices, so the rate change
is **untested territory on PGM hardware**.

RTL: read `ics2115.sv:624`; write is global (not FIFO'd).

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 0E-A | base output rate confirmed EXACT at active_osc=31: hw raw_lrclk_hz=33075 = 33868800/((31+1)*32). Ratio for reduced active_osc NOT measurable via the audio extractor (it resamples to ~44.1kHz; n=23 drifts, n=15 clamps to 44100, n<=7 no capture) — would need an LRCLK edge probe. RTL uses sample_div_period=(n+1)*32 | T-RATE | TESTED-PASS (base) |
| 0E-B | gates the IRQV scan (RTL only scans voices <= active_osc) — does a pending IRQ on a voice above ActiveOsc ever surface? | T-VOICE | TESTED-SIM |
| 0E-C | readback returns 5-bit value | T-RB | TESTED-PASS |

## 0x0F — IRQV (interrupt vector) — Upper8, global, read-only

Evidence: 3 BIOS reads, all `ReadICSRegisterHigh(0xf)`. The IRQ drain loop
(`HandleIrqBit1Service`, ram:153b) reads IRQV repeatedly: voice = `v & 0x1f`,
sources = `v & 0xc0` **active-low** (bit7 clear = osc IRQ, bit6 clear = vol
IRQ), terminates when `(v & 0xc0) == 0xc0` (nothing pending). Init reads it to
flush state.

RTL: scan `ics2115.sv:628-643` — first pending voice with index <= active_osc,
idle value 0xFF; auto-clear at `ics2115.sv:696-719` triggers on **high-byte
read only** and clears **both** pending sources of the matched voice.

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 0F-A | active-low source bits + 5-bit voice; idle read = bits 7:6 set (RTL: 0xFF — are bits 4:0 all-ones on hardware too?) | T-IRQV | TESTED-SIM |
| 0F-B | reading clears the reported voice's pending state — but which? RTL clears both osc+vol at once; hardware may clear only the reported source, or clear on low-byte reads too | T-IRQV | TESTED-SIM |
| 0F-C | scan order/priority with multiple pending voices (RTL: lowest index first) | T-IRQV | TESTED-SIM |
| 0F-D | scan gated by ActiveOsc | T-IRQV | TESTED-SIM |

## 0x10 — OscCtl — Upper8

Evidence: BIOS writes only 0x0f (key-off/reset) and 0x00 (key-on) — 2 sites.
Turtle Beach (TB doc): also writes valid key-on values **0x10/0x20/0x30**
(copied from `wavefront_patch.filterconfig` bits 5:4), transient
`shadow|0x02` while reprogramming a live voice, 0x01 during accumulator-monitor
setup, and **reads** OscCtl treating bit0 = stopped/done. MAME comment
(ics2115.cpp:843): `[7 R | 6 M2 | 5 M1 | 4-2 Reserve | 1 - Timer 2 Strt | 0 -
Timer 1 Strt]` — unimplemented guess that bits 0/1 start the timers.

RTL: read `ics2115.sv:646` returns `{6'b111111, osc_ctl[1:0]}` (upper 6 bits
forced 1); write stores 8 bits, sequencer keys on/off from 0x00/0x0f.

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 10-A | 0x00 = key-on, 0x0f = key-off/reset | T-RB, T-AUD | MODELED |
| 10-B | OscCtl key-on 0x00/0x10/0x20/0x30 produce identical audio on BOTH targets — bits 5:4 (filterconfig) have no standalone audible effect in this configuration | T-AUD | TESTED-PASS |
| 10-C | bit1 as temporary "reprogram gate" (TB `shadow|0x02`): voice halts cleanly and resumes | T-OCTL | TESTED-PASS |
| 10-D | readback: bit0 = stopped/done status (TB reads it); RTL returns stored bits 1:0 with upper bits 1 — is the upper-6 forced-1 shape right? | T-RB, T-OCTL | TESTED-PASS |
| 10-E | MAME timer-start guess for bits 0/1: configure timers, write OscCtl bit0/1, observe timer IRQs | T-OCTL | TESTED-PASS |

## 0x11 — OscSAddr (static address / bank) — Upper8

Evidence: 1 BIOS site (wave-table bank byte). RTL: read `ics2115.sv:649`,
provides ROM address bits 27:20.

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 11-A | bank bits 27:20 of sample ROM address; readback | T-RB | TESTED-PASS |

## 0x12 — VMode — Upper8, **per-voice** (scope disputed)

Evidence: **never accessed by the PGM BIOS** (inventory). Turtle Beach writes
it per-voice during ramp programming, paired with VolIncr (TB doc, "Major
divergence" section ~L1095). MAME treats it as global `m_vmode` with comment
"Unknown variable, seems to be affected by 0x12. Further investigation
Required." (ics2115.h:156-158). Hardware vol_incr sweep showed only bits 1:0
select the rate family; bits 3:2 phase-shift but same period
(docs/ics2115_vol_incr_analysis.md).

RTL: per-voice storage, read `ics2115.sv:652`, only bits 1:0 used by
`calc_vol_step`.

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 12-A | per-voice register (not global): write voice A, read voice B differs | T-VMODE | TESTED-PASS |
| 12-B | bits 1:0 select rate family (mode2 linear; mode0/1/3 exponential, mode0 8 octaves slower via +0 vs +256 offset); bit1 phase/no-op (1==3). RTL reworked to match | T-VINCR2 | TESTED-PASS |
| 12-C | bits 7:2 readback + effect (3:2 phase observation needs re-test) | T-VMODE | TESTED-PASS |

---

# Reserved per-voice range

## 0x13 / 0x14 — written by Turtle Beach, otherwise reserved

Evidence: TB `ResetIcs2115VoiceRegisters` writes 0xffff to both at voice reset;
**no reads observed** (TB doc searched; only reset-time writes). PGM never
touches them. Datasheet: reserved/do-not-access.
RTL: undecoded — write dropped, read returns 0x0000 (`ics2115.sv:654` default).

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 13-A | harmless write-only reserved regs vs. undocumented per-voice state needed for clean reset | T-PROBE | TESTED-PASS |

## 0x15-0x3F — reserved

No usage in any corpus. RTL: 0x15-0x1F hit the voice-case default (read 0);
0x20-0x3F fall to the global-case default (read 0).

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 15-A | read default + write inertness + no aliasing onto real registers | T-PROBE | TESTED-PASS |

---

# Global registers (0x40-0x4F)

RTL read `ics2115.sv:657-675`, write `ics2115.sv:~1146-1180` (decoded:
0x40-0x43, 0x4A, 0x4F only).

## 0x40 / 0x41 — Timer 0 / Timer 1 preset — Lower8

Evidence: timer config writes (`ConfigureSoundChipVoiceMode`, ram:12bb); the
**timer IRQ service reads the fired timer's preset register to acknowledge**
(`HandleIrqBit0Service`, ram:1519: read 0x43, then read 0x40 if bit0 else
0x41 — return value discarded).

RTL: read returns preset (`ics2115.sv:659-660`); read of either byte clears
that timer's pending IRQ (`ics2115.sv:721-733`); write 0 stops the timer.

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 40-A | write sets preset; period = `((scale&0x1f)+1)*(preset+1) << (4+(scale>>5))` (MAME formula, ics2115.cpp:1097-1101) | T-TMR | TESTED-PASS |
| 40-B | hardware: only a full 16-bit read of the preset acks; lo8/hi8 reads do NOT clear (RTL clears on either byte) | T-IRQ | TESTED-PASS |
| 40-C | read value: preset, or live countdown? (BIOS discards it; RTL returns preset) | T-RB, T-IRQ | TESTED-PASS |
| 40-D | preset=0 stops the timer (confirmed both targets) | T-TMR | TESTED-PASS |
| 40-E | hardware: timer-0 scale[4:0]=0 (mult field 0) also stops the timer — no pending, no IRQs (RTL runs at mult=1) | T-TMR | TESTED-PASS |

## 0x42 — Timer 0 scale — Lower8

Evidence: written during timer config (1 site); never read by BIOS.
RTL: write decoded; **read undecoded -> 0x00**.

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 42-A | scale bits [4:0] multiplier, [7:5] shift | T-TMR | TESTED-PASS |
| 42-B | read value on hardware (RTL returns 0) | T-RB | TESTED-PASS |

## 0x43 — Timer control + timer-1 shift (write) / timer IRQ status (read) — Lower8

**HARDWARE-CHARACTERIZED 2026-06-11** (direct experiments, fresh-boot
sessions): the write side is a **timer control register**, not "timer 1
scale":

```
write 0x43:  bit 7:5 = timer-1 shift   (period1 = (preset1+1) << (4+shift))
             bit 4   = timer-1 IRQ output enable
             bit 3   = timer-0 IRQ output enable
             bit 2:0 = no observed rate/delivery effect
read  0x43:  bit 0/1 = timer-0/1 IRQ pending (as modeled)
```

Timer 1 has **no multiplier field** (0xF0/0xF7/0xFF all measure 65.1 Hz with
preset 255) — the timers are asymmetric: timer 0 takes mult+shift from 0x42,
timer 1 takes shift-only from 0x43[7:5]. Measured: 0x43=0xF0 -> 65.1 Hz,
0x43=0x70 -> 1033.3 Hz (shift formula exact); timer 0 with 0x43=0x08 ->
103.1 Hz (full 0x42 formula exact). **Without the 0x43 enable bit the timer
pends (read bit set) but the chip never asserts its IRQ output** — this is
why the first hardware suite run counted zero timer IRQs. The BIOS
ConfigureSoundChipVoiceMode compose `(x*0x20)|(y&0x1f)|0x10` / `|8` now reads
exactly as {shift, enables}.

RTL (now known-wrong): read returns `{6'd0, irq_pending[1:0]}`
(`ics2115.sv:663`) ✓; write sets timer-1 scale ✗ (no enable bits, mult+shift
formula for timer 1).

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 43-A | read bit0/bit1 = timer0/timer1 IRQ pending | T-IRQ | TESTED-PASS |
| 43-B | write = timer control: t1 shift [7:5], t1 enable bit4, t0 enable bit3; t1 has no mult | T-TMR | TESTED-PASS |
| 43-C | reading 0x43 does not ack (confirmed both targets) | T-IRQ | TESTED-PASS |
| 43-D | upper 6 read bits on hardware | T-RB | TESTED-PASS |

## 0x44-0x49 — DMA / accumulator monitor

Evidence: PGM BIOS never touches them (inventory + TB doc explicitly searched
the PGM Z80 driver). Turtle Beach uses **0x48/0x49 as accumulator-monitor data
registers** to read sample memory back through voice 0
(`StartAccumulatorMonitorRead`/`ReadAccumulatorMonitorByte`, TB doc ~L728-758).
MAME: unhandled.
RTL: undecoded (read 0, write dropped).

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 48-A | 0x48/0x49 read bytes of sample memory addressed by voice-0 OscAcc (TB usage) | T-PROBE (extended: program voice 0 acc, read 0x48/0x49) | TESTED-PASS |
| 44-A | 0x44-0x47 defaults/side effects | T-PROBE | TESTED-PASS |

## 0x4A — NOT the IRQ enable — Lower8

Evidence: single BIOS write `0x4A = 1` at end of init; never read by BIOS.
**HARDWARE 2026-06-11: 0x4A is not in the timer-IRQ path at all** — timer 0
fires identically with 0x4A=0x00 or 0x01 (enable is 0x43 bit3), and writes of
0x00/0x03/0xFF all read back constant 0x02. Function unknown; the BIOS's
`0x4A=1` may target something else entirely.
RTL (known-wrong): write sets `irq_enabled` gating the IRQ line; read returns
`irq_pending` (`ics2115.sv:666`, mirrors MAME).

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 4A-A | hardware: 0x4A is the VOICE-IRQ output gate (oneshot delivers with 1, latches silently with 0); timers unaffected | T-VOICE | TESTED-PASS |
| 4A-B | hardware read = constant 0x02 (not pending, not enable) | T-RB | TESTED-PASS |
| 4A-C | pending latches regardless of 0x4A; IRQ line gated by 0x43 enables instead | T-IRQ | TESTED-PASS |

## 0x4B — oscillator IRQ vector? — Lower8, read-only

Evidence: BIOS init self-test (ram:17a6): select voice 0, write
`OscConf = 0xa0` (IRQ pending+enable), poll 0x4B until `(value & 0x9f) ==
0x80`, up to 255 attempts -> sound-chip error code 2 on timeout. Mask keeps
bit7 + bits 4:0 -> expects bit7 **set** and voice bits = 0. So 0x4B reads like
an osc-IRQ vector with **active-high** valid bit + voice index — opposite
polarity to IRQV(0x0F). GF1 heritage: IRQ-source register returning the
interrupting voice.

RTL: stub, always returns 0x80 (`ics2115.sv:668-669`) — passes the BIOS
self-test **only because the BIOS tests voice 0**.

**HARDWARE 2026-06-11**: idle reads 0x02 (not 0x80); with a host-pended osc
IRQ on voice 0 it reads 0x80 on the **first** poll (BIOS self-test replay
succeeded in 1 iteration). The RTL constant-0x80 stub is wrong at idle and
untested for voice != 0.

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 4B-A | returns `0x80 | voice` while an osc IRQ pends (hw confirmed voice 0; voice != 0 still to record) | T-SYS | TESTED-PASS |
| 4B-B | idle value = 0x02 on hardware (RTL stub reads 0x80) | T-RB | TESTED-PASS |
| 4B-C | bits 6:5 meaning (masked out by BIOS) | T-SYS | TESTED-PASS |

## 0x4C — chip revision — Lower8

Evidence: BIOS **writes** `0x4C = 3` during init (ram:16a0) before voice init;
revision is read via the port-echo probe sequence and must equal 1
(`VerifySoundChipOrDisplayError` hangs forever otherwise).
RTL: read-only constant 0x01 (`ics2115.sv:672`); write dropped.

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 4C-A | reads 0x01 on PGM's chip | T-RB | TESTED-PASS |
| 4C-B | write-insensitive readback confirmed on both targets (side effects of the BIOS write-3 still unknown) | T-SYS | TESTED-PASS |

## 0x4D — system control — Lower8

Evidence (all BIOS, richest unknown register — 8 sites):
- `ResetSoundChipMixerState` (ram:173b): write 0x00, **16 dummy reads of 0x4D**
  (settle delay), write 0x01 -> bit0 looks like run/~reset.
- `InitializeSoundChipVoicesAndTables` entry (ram:17a6): RMW `& 0xf7` (clear
  bit3) before voice init; at exit writes `0x4D = 0x00` (the operand is the
  high byte of the caller's literal argument 0x004c, i.e. zero — resolved
  question G-03).
- `InitializeSoundChipCore` (ram:16a0): after voice init, RMW `| 0x0c` (set
  bits 3:2), then enables IRQs (0x4A=1).
- Net post-init state: 0x4D = 0x0c (bits 3:2 set, bit0 **clear**).

MAME: unhandled. RTL: **undecoded** — writes dropped, reads return 0.

**HARDWARE 2026-06-11**: stored/readable bit mask = **0x05** (walking writes:
only bits 0 and 2 read back; 0xFF reads 0x05). Bit 3 read as set in the boot
state (0x0D) but does not store from writes — status-ish. **Bit 0 gates the
timers**: with bit0 clear, timer pending never latches (0x43 reads 0); with
bit0 set, pending latches. Voice-register access works regardless of 0x4D.
The driver/BIOS boot state is 0x0D. Walking all bit values does NOT wedge the
register interface.

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 4D-A | readback: stored mask 0x05 only (RTL stores nothing) | T-RB | TESTED-PASS |
| 4D-B | bit0 gates timer counting (pending never latches when clear); BIOS strobe = stop/start | T-IRQ | TESTED-PASS |
| 4D-C | bits 0 AND 2 are the MASTER CHIP-RUN gate (hw 2026-06-13 gate-scope): with them clear the whole oscillator engine freezes — audio rms=0, OscAcc does not advance, no timers, no IRQs. RTL gates sample_tick + mutes output + timer/IRQ on bit0&bit2. bit3 reads set at boot, doesn't store | T-SYS,T-AUD | TESTED-PASS |
| 4D-D | RTL must model: stored mask 0x05, bit0 timer gate | — | TESTED-PASS |

## 0x4E — unused

No usage in any corpus. RTL: undecoded.

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 4E-A | default read / write inertness | T-PROBE | TESTED-PASS |

## 0x4F — oscillator select — Lower8, write-only(?)

Evidence: 7 BIOS sites via `WriteICSSelectOscillator`; never read.
RTL: write sets `osc_select` (`ics2115.sv:~1170`); read undecoded -> 0.

| ID | Assumed behavior | Test | Status |
|---|---|---|---|
| 4F-A | selects voice for indexed regs 0x00-0x12 | T-RB (isolation: write voice A's reg, verify voice B unchanged) | TESTED-PASS |
| 4F-B | hardware: readable, returns the selected voice (0x00/0x07/0x1f echo back; RTL reads 0) | T-RB | TESTED-PASS |
| 4F-C | values > 0x1f: masked to 5 bits? | T-PROBE | UNKNOWN |

---

### VIncr/VMode ramp-rate table (hardware-measured 2026-06-13, T-VINCR)

Ramp duration in vblanks for a full VolStart=0 -> VolEnd=0xFF window (longer =
slower step).  sim/hw; '-' = no completion IRQ within 600 vblanks (slow) or
single-step overshoot past VolEnd (fast cells).

```
vmode\vincr    1     2     4     8    16    32    64   128   255
  0          -     -     -     -     -     -     -     -   119/473
  1        28/453 28/438 26/415 23/372 20/311 15/233 7/116 29/27  2/-
  2       117/116 58/57 29/27 15/15  7/6   4/3   2/-   1/-   1/-
  3        28/453 28/438 26/415 23/373 20/311 15/233 7/115 29/27  2/-
```

VERDICT: RTL calc_vol_step is correct ONLY for vmode 2 (fast family).  vmode
1 and 3 (identical) are ~16x too fast and nearly VIncr-independent in the RTL,
while hardware ramps slowly with a strong, compressed VIncr dependence
(rate ~flat for vincr 1-16, then climbs steeply: doubling vincr 64->128
quadruples the rate -> a log/exponential step law).  vmode 0 (slowest) is ~4x
too fast in the RTL.  Fast cells overshoot VolEnd in one step (no completion
IRQ) on hardware; the RTL completes them.

RTL FIX NEEDED (not yet implemented): rework calc_vol_step / the vol-rate
table in ics2115_osc.sv + ics2115_tables.sv to reproduce the slow-family
log-domain curve derived from this table.  vmode bit1 is a phase/no-op for
rate (1==3).

# Resolved static-analysis questions (G-tasks)

| ID | Question | Resolution |
|---|---|---|
| G-01 | Turtle Beach OscCtl 0x10/0x20/0x30 origin + readback | From `wavefront_patch` byte+6 bits 5:4 (`filterconfig`), copied at key-on (`StartMidiVoiceForSample`); OscCtl **is** read back, bit0 treated as stopped/done (TB doc L146-165, 298-311). Hardware meaning of bits 5:4 -> T-OCTL. |
| G-02 | 0x13/0x14 ever read? | No — only reset-time 0xffff writes (TB doc searched). Re-verify needs `turtle_beach.gpr` opened in Ghidra (the OSWF.bin copy in the running ketbl project is unanalyzed). |
| G-03 | Value written to 0x4D at decompile L1621 | `(0x004c >> 8) = 0x00` — the decompiler reused the caller's literal argument slot. Sequence fully reconstructed under 0x4D above. |
| G-04 | 0x4B poll loop semantics | Init self-test: force `OscConf=0xa0` on voice 0, poll 0x4B for `(v&0x9f)==0x80`, error code 2 on 255-try timeout. Implies active-high osc-IRQ vector; RTL stub passes by coincidence. |
| G-05 | DYNAMIC register indices in inventory | None — all 61 BIOS call sites use literal register numbers (only some *values* are dynamic, all table-driven voice parameters). |

# Known coverage gaps

- **Game Z80 drivers** (theglad, espgaluda, kov2 etc.) are not in the text
  inventory — only Ghidra projects exist. TB doc conclusion 8 covers
  espgaluda's conservative OscCtl usage; a fuller audit can extend
  `util/ics_access_inventory.py` once decompile text exports exist.
- **`ReadIrqStatusPort` has no decompiled caller** — the BIOS IRQ entry is
  likely asm-level (vector at ram:0038?); the port-0 dispatch evidence comes
  from game drivers. Worth confirming the BIOS IRQ entry path in Ghidra.
- Timer absolute clock base: T-TMR measures against vblank; the sim/hw vblank
  rates are the same RTL, but the chip input clock divisor itself is taken
  from MAME and untested.
