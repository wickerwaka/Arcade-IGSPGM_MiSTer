---
name: pgm-sim-server
description: Control the MiSTer PGM simulator through its persistent stdio JSON server. Use when you need structured simulator automation, including loading games or MRAs, running until conditions, reading CPU state, reading or writing memory, saving states, tracing, or taking screenshots.
---

# PGM Simulator Server

Use this skill when working with the simulator in `sim/` through its machine-readable control interface.

Primary reference:

- [`docs/sim-server.md`](../../../docs/sim-server.md)

## Workflow

1. Start a persistent server process:
   ```bash
   cd /Users/akawaka/Source/Arcade-IGSPGM_MiSTer/sim
   ./sim --server
   ```
2. Send exactly one JSON request per line on `stdin`.
3. Read exactly one JSON response per request from `stdout`.
4. Treat `stderr` as logs only.

## Rules

- Always call `sim.initialize` before other control methods.
- Then call either `sim.load_game` or `sim.load_mra`.
- For normal game startup, call `sim.reset` after `sim.load_game`.
- Prefer `sim.run_until` over many tiny `sim.run_cycles` calls when synchronizing to a CPU or signal condition.
- Use `cpu.get_state`, `memory.read`, and `signal.read` for observations.
- Use `signal.list` to discover which VPI signals are available in the current build.
- For internal HDL signals, use VPI-resolved names like `sim_top.vblank` or `pgm_inst.cpu_word_addr`.
- Use `state.save` / `state.load` for checkpoints.
- Call `sim.shutdown` before ending the session when practical.

## Recommended request sequence

```json
{"id":1,"method":"sim.initialize","params":{"headless":true}}
{"id":2,"method":"sim.load_game","params":{"name":"pgm"}}
{"id":3,"method":"sim.reset","params":{"cycles":100}}
{"id":4,"method":"sim.run_until","params":{"condition":{"type":"signal_equals","signal":"vblank","value":1},"timeout_cycles":1000000}}
{"id":5,"method":"cpu.get_state","params":{}}
```

## Important methods

- `sim.initialize`
- `sim.shutdown`
- `sim.status`
- `sim.load_game`
- `sim.load_mra`
- `sim.reset`
- `sim.run_cycles`
- `sim.run_frames`
- `sim.run_until`
- `cpu.get_state`
- `memory.read`
- `memory.write`
- `memory.list_regions`
- `signal.read`
- `signal.list`
- `state.list`
- `state.save`
- `state.load`
- `trace.start`
- `trace.stop`
- `video.screenshot`
- `input.set_dipswitch_a`
- `input.set_dipswitch_b`

Read the full method reference in [`docs/sim-server.md`](../../../docs/sim-server.md) before using less common operations or composing complex conditions.

## Condition examples

Wait for vblank:

```json
{"type":"signal_equals","signal":"vblank","value":1}
```

Wait for an internal HDL signal using VPI name lookup:

```json
{"type":"signal_not_equals","signal":"pgm_inst.cpu_word_addr","value":0}
```

Wait for a specific PC:

```json
{"type":"cpu_pc_equals","value":4096}
```

Wait for a PC range while not in reset:

```json
{
  "type":"and",
  "children":[
    {"type":"cpu_pc_in_range","start":4096,"end":8192},
    {"type":"not","children":[{"type":"signal_equals","signal":"reset","value":1}]}
  ]
}
```

## Error handling

If a request fails:

1. inspect `error.code`
2. inspect `error.message`
3. if state may be ambiguous, call `sim.status`
4. if initialization or load failed, restart from `sim.initialize`

## Notes

- The protocol is synchronous: one request, one response.
- Responses are JSON lines on `stdout`.
- Logs may still appear on `stderr`.
- Keep a single long-lived simulator process per investigation when possible.
- Signal lookup first checks built-in aliases, then tries VPI hierarchical lookup.
- Default VPI builds may expose only a subset of internal signals; use `signal.list` to inspect availability.
