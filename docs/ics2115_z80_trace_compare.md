# ICS2115 Z80 trace comparison

Captured 2026-05-20 with simulator server, FST traces (5-frame windows):

- Good BIOS-driver path (`z80_sound_test`, START pressed): `/tmp/z80_sound_start.fst`
- Debug driver path (`z80_ics_test`, PLAY pressed): `/tmp/z80_ics_play.fst`

The FSTs were loaded/opened by Surfer/WCP successfully.  The tabulation below was extracted from the FST host bus signals:

- `TOP.sim_top.pgm_inst.ics2115_addr[1:0]`
- `TOP.sim_top.pgm_inst.ics2115_wr_n`
- `TOP.sim_top.pgm_inst.ics2115_rd_n`
- `TOP.sim_top.pgm_inst.z80_dout[7:0]`
- `TOP.sim_top.pgm_inst.ics2115_dout[7:0]`
- `TOP.sim_top.pgm_inst.ics2115.reg_select[7:0]`
- `TOP.sim_top.pgm_inst.ics2115.osc_select[4:0]`
- `TOP.sim_top.pgm_inst.ics2115.active_osc[4:0]`

## Port convention

- port 1 = register select (`0x8001`)
- port 2 = low data (`0x8002`)
- port 3 = high data (`0x8003`)
- port 0 = status (`0x8000`)

## `z80_sound_test` / BIOS-driver path

Before voice programming, the BIOS driver is servicing timers:

```text
W p1 SEL 0x40 TIMER0
R p2 TIMER0 -> 0x9b
R p0 STATUS -> 0x00 / 0x81
W p1 SEL 0x43 TIMER_STAT/SCALE1
R p2 TIMER_STAT/SCALE1 -> 0x03
```

The actual voice-0 programming sequence after START is:

```text
W p1 SEL 0x4f OSC_SELECT
W p2 OSC_SELECT <= 0x00

W p1 SEL 0x01 OSC_FC
W p2 OSC_FC low  <= 0x55
W p3 OSC_FC high <= 0x01       ; FC = 0x0155

W p1 SEL 0x11 OSC_SADDR
W p3 OSC_SADDR <= 0x40

W p1 SEL 0x0b OSC_ACC_L
W p2 OSC_ACC_L low  <= 0x00
W p3 OSC_ACC_L high <= 0x60    ; ACC_L = 0x6000

W p1 SEL 0x0a OSC_ACC_H
W p2 OSC_ACC_H low  <= 0x3a
W p3 OSC_ACC_H high <= 0xb6    ; ACC_H = 0xb63a

W p1 SEL 0x03 OSC_START_L
W p2 OSC_START_L low  <= 0x00
W p3 OSC_START_L high <= 0x60  ; START_L = 0x6000

W p1 SEL 0x02 OSC_START_H
W p2 OSC_START_H low  <= 0x3a
W p3 OSC_START_H high <= 0xb6  ; START_H = 0xb63a

W p1 SEL 0x05 OSC_END_L
W p2 OSC_END_L low  <= 0x00
W p3 OSC_END_L high <= 0xb0    ; END_L = 0xb000

W p1 SEL 0x04 OSC_END_H
W p2 OSC_END_H low  <= 0x1e
W p3 OSC_END_H high <= 0xb8    ; END_H = 0xb81e

W p1 SEL 0x0c PAN
W p3 PAN <= 0x7f

W p1 SEL 0x09 VOL_ACC
W p2 VOL_ACC low  <= 0xf0
W p3 VOL_ACC high <= 0xdf      ; VOL_ACC = 0xdff0

W p1 SEL 0x00 OSC_CONF
W p3 OSC_CONF <= 0x20

W p1 SEL 0x0d VOL_CTRL
W p3 VOL_CTRL <= 0x03

W p1 SEL 0x10 OSC_CTL
W p3 OSC_CTL <= 0x00           ; key on
```

Simulator state after this sequence:

```text
active_osc = 31
reg_select = 0x40
osc_select = 0
voice0.state_on = true
voice0.osc_conf = 0x20
voice0.vol_ctrl = 0x03
voice0.osc_ctl  = 0x00
voice0.osc_saddr = 0x40
```

## `z80_ics_test` / debug-driver path

PLAY produces this voice-0 sequence:

```text
W p1 SEL 0x0e ACTIVE_OSC
W p3 ACTIVE_OSC <= 0x1f

W p1 SEL 0x4f OSC_SELECT
W p2 OSC_SELECT <= 0x00

W p1 SEL 0x4f OSC_SELECT
W p2 OSC_SELECT <= 0x00
W p1 SEL 0x10 OSC_CTL
W p3 OSC_CTL <= 0x0f           ; explicit stop before programming

W p1 SEL 0x4f OSC_SELECT
W p2 OSC_SELECT <= 0x00
W p1 SEL 0x00 OSC_CONF
W p3 OSC_CONF <= 0x08

W p1 SEL 0x4f OSC_SELECT
W p2 OSC_SELECT <= 0x00
W p1 SEL 0x01 OSC_FC
W p2 OSC_FC low  <= 0xa7
W p3 OSC_FC high <= 0x2a       ; FC = 0x2aa7 (RTL stores even 0x2aa6)

W p1 SEL 0x4f OSC_SELECT
W p2 OSC_SELECT <= 0x00
W p1 SEL 0x02 OSC_START_H
W p2 OSC_START_H low  <= 0x20
W p3 OSC_START_H high <= 0x21  ; START_H = 0x2120

W p1 SEL 0x4f OSC_SELECT
W p2 OSC_SELECT <= 0x00
W p1 SEL 0x03 OSC_START_L
W p3 OSC_START_L high <= 0x00  ; START_L high only

W p1 SEL 0x4f OSC_SELECT
W p2 OSC_SELECT <= 0x00
W p1 SEL 0x04 OSC_END_H
W p2 OSC_END_H low  <= 0x8b
W p3 OSC_END_H high <= 0x2a    ; END_H = 0x2a8b

W p1 SEL 0x4f OSC_SELECT
W p2 OSC_SELECT <= 0x00
W p1 SEL 0x05 OSC_END_L
W p3 OSC_END_L high <= 0x00    ; END_L high only

W p1 SEL 0x4f OSC_SELECT
W p2 OSC_SELECT <= 0x00
W p1 SEL 0x06 VOL_INCR
W p2 VOL_INCR low <= 0x00      ; differs from high-byte style used by many voice regs

W p1 SEL 0x4f OSC_SELECT
W p2 OSC_SELECT <= 0x00
W p1 SEL 0x07 VOL_START
W p2 VOL_START low <= 0xff

W p1 SEL 0x4f OSC_SELECT
W p2 OSC_SELECT <= 0x00
W p1 SEL 0x08 VOL_END
W p2 VOL_END low <= 0xff

W p1 SEL 0x4f OSC_SELECT
W p2 OSC_SELECT <= 0x00
W p1 SEL 0x09 VOL_ACC
W p2 VOL_ACC low  <= 0xff
W p3 VOL_ACC high <= 0xff      ; VOL_ACC = 0xffff

W p1 SEL 0x4f OSC_SELECT
W p2 OSC_SELECT <= 0x00
W p1 SEL 0x0a OSC_ACC_H
W p2 OSC_ACC_H low  <= 0x20
W p3 OSC_ACC_H high <= 0x21    ; ACC_H = 0x2120

W p1 SEL 0x4f OSC_SELECT
W p2 OSC_SELECT <= 0x00
W p1 SEL 0x0b OSC_ACC_L
W p2 OSC_ACC_L low  <= 0x00
W p3 OSC_ACC_L high <= 0x00    ; ACC_L = 0x0000

W p1 SEL 0x4f OSC_SELECT
W p2 OSC_SELECT <= 0x00
W p1 SEL 0x0c PAN
W p3 PAN <= 0x7f

W p1 SEL 0x4f OSC_SELECT
W p2 OSC_SELECT <= 0x00
W p1 SEL 0x0d VOL_CTRL
W p3 VOL_CTRL <= 0x00

W p1 SEL 0x4f OSC_SELECT
W p2 OSC_SELECT <= 0x00
W p1 SEL 0x11 OSC_SADDR
W p3 OSC_SADDR <= 0x00

W p1 SEL 0x4f OSC_SELECT
W p2 OSC_SELECT <= 0x00
W p1 SEL 0x10 OSC_CTL
W p3 OSC_CTL <= 0x00           ; key on
```

Simulator state after this sequence:

```text
active_osc = 31
reg_select = 0x10
osc_select = 0
voice0.state_on = true
voice0.osc_conf = 0x08
voice0.vol_ctrl = 0x01      ; RTL readback/state after writing 0x00
voice0.osc_ctl  = 0x00
voice0.osc_saddr = 0x00
```

## Main differences

1. The BIOS path selects voice once and then writes the voice registers.  The debug path re-selects voice 0 before every voice register.  This works in the simulator, but it is not BIOS-like.

2. The BIOS path does not write `ACTIVE_OSC` during this START trace.  It relies on prior chip init/default state.  The debug path writes `0x0e = 0x1f` immediately before PLAY.

3. The BIOS path uses:
   - `OSC_CONF = 0x20`
   - `VOL_CTRL = 0x03`
   - `VOL_ACC = 0xdff0`
   - `OSC_SADDR = 0x40`

   The debug path uses:
   - `OSC_CONF = 0x08`
   - `VOL_CTRL = 0x00`
   - `VOL_ACC = 0xffff`
   - `OSC_SADDR = 0x00`

4. Earlier debug paths wrote `VOL_INCR`, `VOL_START`, and `VOL_END` through the low data port (`0x8002`).  Hardware testing and the current RTL treat these as high-byte voice-register accesses via `0x8003`; debug/test code should use `Z80_ICS_WIDTH_UPPER8` for `0x06`, `0x07`, and `0x08`.

5. The BIOS path starts from a known-good wave-table entry.  The debug path programs custom sample addresses directly.  The simulator can play this, but for hardware isolation the next test should mimic the known-good BIOS wave entry exactly.

## Suggested next hardware test

Make `z80_ics_test` PLAY program voice 0 with the exact BIOS-driver values from the good trace:

```text
voice = 0
FC        = 0x0155
SADDR     = 0x40
ACC_L     = 0x6000
ACC_H     = 0xb63a
START_L   = 0x6000
START_H   = 0xb63a
END_L     = 0xb000
END_H     = 0xb81e
PAN       = 0x7f
VOL_ACC   = 0xdff0
OSC_CONF  = 0x20
VOL_CTRL  = 0x03
OSC_CTL   = 0x00
```

Also avoid re-selecting `0x4f` before every single register for this specific hardware-parity test; select voice once, then emit the same register order as the BIOS trace.
