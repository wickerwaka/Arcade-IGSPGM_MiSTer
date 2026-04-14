# Simulator server interface

The simulator can run as a persistent, agent-friendly control server over stdio.

## Start the server

```bash
cd sim
./sim --server
```

Alias:

```bash
./sim --control-stdio
```

In server mode:

- requests are read from `stdin`
- one JSON object is expected per line
- one JSON response is written to `stdout` per request
- simulator logs and incidental output go to `stderr`

This makes `stdout` safe for machine parsing.

## Request format

Each request must be a single-line JSON object:

```json
{"id":1,"method":"sim.status","params":{}}
```

Fields:

- `id` - required numeric request id, echoed in the response
- `method` - required string method name
- `params` - optional object of method parameters

## Response format

Successful response:

```json
{"id":1,"ok":true,"result":{"initialized":true}}
```

Error response:

```json
{"id":1,"ok":false,"error":{"code":"bad_request","message":"Missing field: method"}}
```

Fields:

- `id` - request id, or `0` if parsing failed before an id was available
- `ok` - boolean success flag
- `result` - method-specific payload on success
- `error.code` - stable machine-readable error code
- `error.message` - human-readable error text

## Lifecycle

Typical session:

1. `sim.initialize`
2. `sim.load_game` or `sim.load_mra`
3. optional `sim.reset`
4. query and control methods like `sim.run_cycles`, `cpu.get_state`, `memory.read`
5. `sim.shutdown` when finished

## Methods

### `sim.initialize`

Initialize the simulator runtime.

Request:

```json
{"id":1,"method":"sim.initialize","params":{"headless":true}}
```

Params:

- `headless` - optional boolean, defaults to `true`

Result:

```json
{}
```

### `sim.shutdown`

Shut down the simulator runtime.

Request:

```json
{"id":2,"method":"sim.shutdown","params":{}}
```

Result:

```json
{}
```

### `sim.status`

Return current simulator status.

Request:

```json
{"id":3,"method":"sim.status","params":{}}
```

Result:

```json
{
  "initialized": true,
  "running": false,
  "paused": false,
  "trace_active": false,
  "headless": true,
  "total_ticks": 0,
  "game_name": "pgm"
}
```

### `sim.load_game`

Load a named built-in game target.

Request:

```json
{"id":4,"method":"sim.load_game","params":{"name":"pgm"}}
```

Params:

- `name` - game short name

Result:

```json
{}
```

Possible errors include:

- `unknown_game`
- `load_failed`

### `sim.load_mra`

Load an MRA file.

Request:

```json
{"id":5,"method":"sim.load_mra","params":{"path":"../releases/finalb.mra"}}
```

Params:

- `path` - path to the MRA file

Result:

```json
{}
```

### `sim.reset`

Assert reset for a fixed number of cycles.

Request:

```json
{"id":6,"method":"sim.reset","params":{"cycles":100}}
```

Params:

- `cycles` - number of cycles to hold reset

Result:

```json
{}
```

### `sim.run_cycles`

Run a fixed number of cycles.

Request:

```json
{"id":7,"method":"sim.run_cycles","params":{"count":1000}}
```

Params:

- `count` - cycles to execute

Result:

```json
{
  "reason": "completed",
  "ticks_executed": 1000,
  "frames_executed": 0
}
```

### `sim.run_frames`

Run a fixed number of video frames using vblank transitions.

Request:

```json
{"id":8,"method":"sim.run_frames","params":{"count":1}}
```

Params:

- `count` - frames to execute

Result:

```json
{
  "reason": "completed",
  "ticks_executed": 896120,
  "frames_executed": 1
}
```

### `sim.run_until`

Run until a condition is satisfied or until a stop reason occurs.

For `signal_*` conditions, the signal may be either:

- one of the built-in aliases like `vblank`
- a VPI-resolved hierarchical signal name such as `sim_top.vblank` or `pgm_inst.cpu_word_addr`

For VPI lookup, the controller tries these forms automatically:

- exact name provided
- `TOP.<name>`
- `sim_top.<name>`
- `TOP.sim_top.<name>`

Note: the default VPI build does **not** use `--public-flat-rw`, so only a subset of internal signals may be visible through VPI.
Built-in aliases remain available regardless.

Request:

```json
{
  "id": 9,
  "method": "sim.run_until",
  "params": {
    "condition": {
      "type": "cpu_pc_equals",
      "value": 256
    },
    "timeout_cycles": 1000000
  }
}
```

Params:

- `condition` - required condition object
- `timeout_cycles` - optional cycle timeout

Result:

```json
{
  "reason": "condition_met",
  "ticks_executed": 2048,
  "frames_executed": 0
}
```

#### Condition types

Supported condition `type` values:

- `signal_equals`
- `signal_not_equals`
- `signal_less_than`
- `signal_less_equal`
- `signal_greater_than`
- `signal_greater_equal`
- `cpu_pc_equals`
- `cpu_pc_in_range`
- `cpu_pc_out_of_range`
- `and`
- `or`
- `not`

Condition fields depend on the type:

##### Signal comparisons

```json
{"type":"signal_equals","signal":"vblank","value":1}
```

VPI-resolved example:

```json
{"type":"signal_not_equals","signal":"pgm_inst.cpu_word_addr","value":0}
```

Fields:

- `signal` - signal name
- `value` - expected integer value

##### Program counter equality

```json
{"type":"cpu_pc_equals","value":4096}
```

##### Program counter range

Either `start`/`end` or internal `value`/`value2` mapping is accepted by the protocol parser.

```json
{"type":"cpu_pc_in_range","start":4096,"end":8192}
```

##### Boolean composition

```json
{
  "type": "and",
  "children": [
    {"type":"signal_equals","signal":"vblank","value":1},
    {"type":"cpu_pc_in_range","start":4096,"end":8192}
  ]
}
```

`not` uses the first child:

```json
{
  "type": "not",
  "children": [
    {"type":"signal_equals","signal":"reset","value":1}
  ]
}
```

#### Run stop reasons

Possible `reason` values:

- `completed`
- `condition_met`
- `watchpoint_hit`
- `timeout`
- `error`

## CPU and memory inspection

### `cpu.get_state`

Request:

```json
{"id":10,"method":"cpu.get_state","params":{}}
```

Result:

```json
{
  "pc": 256,
  "registers": [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
  "disasm": "move.w d0, d1"
}
```

### `memory.list_regions`

Request:

```json
{"id":11,"method":"memory.list_regions","params":{}}
```

Result:

```json
[
  "BIOS_ROM",
  "PROGRAM_ROM",
  "PALETTE_RAM",
  "VIDEO_RAM",
  "WORK_RAM",
  "AUDIO_RAM",
  "TILE_ROM",
  "MUSIC_ROM",
  "B_ROM",
  "A_ROM"
]
```

### `memory.read`

Read raw bytes from a named region.

Request:

```json
{"id":12,"method":"memory.read","params":{"region":"WORK_RAM","address":0,"size":16}}
```

Params:

- `region` - one of the region names above
- `address` - byte address within the region
- `size` - number of bytes to read

Result:

```json
{
  "region": "WORK_RAM",
  "address": 0,
  "data_hex": "000102030405060708090a0b0c0d0e0f"
}
```

### `memory.write`

Write raw bytes to a named region.

Request:

```json
{"id":13,"method":"memory.write","params":{"region":"WORK_RAM","address":0,"data_hex":"01020304"}}
```

Params:

- `region` - region name
- `address` - byte address within the region
- `data_hex` - even-length hex byte string

Result:

```json
{}
```

## Signal inspection

### `signal.read`

Read a signal value.

Request:

```json
{"id":14,"method":"signal.read","params":{"name":"vblank"}}
```

Result:

```json
{"name":"vblank","value":0,"width":1,"value_hex":"0"}
```

Names may be:

- built-in aliases:
  - `vblank`
  - `hblank`
  - `reset`
  - `rom_load_busy`
  - `ss_state_out`
- VPI hierarchical names, for example:
  - `sim_top.vblank`
  - `pgm_inst.cpu_word_addr`
  - `TOP.sim_top.pgm_inst.cpu_word_addr`

Notes:

- VPI reads currently support signals up to 64 bits wide.
- Signals containing X/Z bits currently return an error.
- Not every internal signal is necessarily exposed by the default VPI build.

### `signal.list`

Enumerate built-in aliases and VPI-visible signals available at runtime.

Request:

```json
{"id":14,"method":"signal.list","params":{}}
```

Result:

```json
{
  "signals": [
    {"name":"vblank","width":1,"kind":"alias","source":"builtin"},
    {"name":"sim_top.pgm_inst.cpu_word_addr","width":24,"kind":"reg","source":"vpi"}
  ]
}
```

This is the best way to discover what VPI signals are actually available in the current build.

## State management

### `state.list`

Request:

```json
{"id":15,"method":"state.list","params":{}}
```

Result:

```json
{"states":["000.pgmstate","001.pgmstate"]}
```

### `state.save`

Request:

```json
{"id":16,"method":"state.save","params":{"filename":"000.pgmstate"}}
```

Result:

```json
{}
```

### `state.load`

Request:

```json
{"id":17,"method":"state.load","params":{"filename":"000.pgmstate"}}
```

Result:

```json
{}
```

## Trace and video

### `trace.start`

Request:

```json
{"id":18,"method":"trace.start","params":{"filename":"trace.fst","depth":4}}
```

Params:

- `filename` - trace output path
- `depth` - optional trace depth, defaults to `1`

Result:

```json
{}
```

### `trace.stop`

Request:

```json
{"id":19,"method":"trace.stop","params":{}}
```

Result:

```json
{}
```

### `video.screenshot`

Request:

```json
{"id":20,"method":"video.screenshot","params":{"path":"frame.png"}}
```

Result:

```json
{"path":"frame.png"}
```

## Input control

### `input.set_dipswitch_a`

Request:

```json
{"id":21,"method":"input.set_dipswitch_a","params":{"value":1}}
```

### `input.set_dipswitch_b`

Request:

```json
{"id":22,"method":"input.set_dipswitch_b","params":{"value":0}}
```

Both return:

```json
{}
```

## Error handling notes

Common error codes include:

- `bad_request`
- `unknown_method`
- `not_initialized`
- `unknown_game`
- `load_failed`
- `invalid_region`
- `invalid_signal`
- `save_state_failed`
- `load_state_failed`
- `screenshot_failed`

## Example session

```text
> {"id":1,"method":"sim.initialize","params":{"headless":true}}
< {"id":1,"ok":true,"result":{}}

> {"id":2,"method":"sim.load_game","params":{"name":"pgm"}}
< {"id":2,"ok":true,"result":{}}

> {"id":3,"method":"sim.reset","params":{"cycles":100}}
< {"id":3,"ok":true,"result":{}}

> {"id":4,"method":"sim.run_until","params":{"condition":{"type":"signal_equals","signal":"vblank","value":1},"timeout_cycles":1000000}}
< {"id":4,"ok":true,"result":{"reason":"condition_met","ticks_executed":447980,"frames_executed":0}}

> {"id":5,"method":"cpu.get_state","params":{}}
< {"id":5,"ok":true,"result":{"pc":256,"registers":[...],"disasm":"..."}}
```

## Agent usage guidance

For agent control, prefer:

- one long-lived `./sim --server` process per task
- monotonically increasing request ids
- `sim.status` after setup failures
- `sim.run_until` for synchronization instead of polling with many tiny `sim.run_cycles` calls
- `memory.read` / `signal.read` for explicit observations

Avoid mixing machine-readable protocol traffic with ad-hoc terminal interaction on the same stdio stream.
