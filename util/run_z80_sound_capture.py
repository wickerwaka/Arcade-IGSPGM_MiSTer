#!/usr/bin/env python3
"""Run z80_sound_test in the simulator and capture audio.

This script builds the z80_sound_test page by default, starts the simulator
JSON server, waits for the test ROM to initialize, sets the TestROM GUI values,
starts audio capture, presses Start, runs for the requested number of frames,
and shuts the simulator down.
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import subprocess
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[1]
SIM_DIR = REPO_ROOT / "sim"
TEST_STATUS_ADDR = 0x1F000
TEST_STATUS_SIZE = 30
TEST_STATUS_MAGIC = 0x5A53


def log(message: str) -> None:
    print(f"[z80-capture] {message}", flush=True)


def status_summary(status: dict[str, int]) -> str:
    return (
        f"frame={status.get('frame', 0)} "
        f"init={status.get('initialized', 0)} "
        f"verify={status.get('verify_errors', 0)} "
        f"cmd={status.get('commands_sent', 0)} "
        f"wave={status.get('selected_wave', 0)} "
        f"repeat={status.get('repeat_count', 0)} "
        f"delay={status.get('repeat_delay_frames', 0)} "
        f"playing={status.get('playing', 0)} "
        f"done={status.get('done', 0)} "
        f"phase={status.get('phase', 0)}"
    )


class SimServer:
    def __init__(self, sim_path: Path, env: dict[str, str] | None = None):
        self._next_id = 1
        self._proc = subprocess.Popen(
            [str(sim_path), "--server"],
            cwd=SIM_DIR,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
            env=env,
        )

    def close(self) -> None:
        if self._proc.poll() is None:
            try:
                self.call("sim.shutdown")
            except Exception:
                pass
        if self._proc.stdin:
            try:
                self._proc.stdin.close()
            except Exception:
                pass
        try:
            self._proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self._proc.kill()
            self._proc.wait(timeout=10)

    def call(self, method: str, params: dict[str, Any] | None = None) -> Any:
        if self._proc.poll() is not None:
            stderr = self._proc.stderr.read() if self._proc.stderr else ""
            raise RuntimeError(f"simulator exited before {method}: {stderr}")
        if not self._proc.stdin or not self._proc.stdout:
            raise RuntimeError("simulator stdio is unavailable")

        req_id = self._next_id
        self._next_id += 1
        self._proc.stdin.write(json.dumps({"id": req_id, "method": method, "params": params or {}}) + "\n")
        self._proc.stdin.flush()

        line = self._proc.stdout.readline()
        if not line:
            stderr = self._proc.stderr.read() if self._proc.stderr else ""
            raise RuntimeError(f"no response for {method}: {stderr}")

        resp = json.loads(line)
        if not resp.get("ok"):
            raise RuntimeError(f"{method} failed: {resp}")
        return resp.get("result")


def parse_test_status(data_hex: str) -> dict[str, int]:
    data = bytes.fromhex(data_hex)
    words = struct.unpack(">" + "H" * (len(data) // 2), data)
    names = [
        "magic",
        "initialized",
        "verify_errors",
        "commands_sent",
        "selected_wave",
        "repeat_count",
        "repeat_delay_frames",
        "playing",
        "done",
        "latch1",
        "latch2",
        "latch3",
        "phase",
        "frame_hi",
        "frame_lo",
    ]
    status = {name: words[i] for i, name in enumerate(names)}
    status["frame"] = (status["frame_hi"] << 16) | status["frame_lo"]
    return status


def read_status(sim: SimServer) -> dict[str, int]:
    result = sim.call(
        "memory.read",
        {"region": "WORK_RAM", "address": TEST_STATUS_ADDR, "size": TEST_STATUS_SIZE},
    )
    return parse_test_status(result["data_hex"])


def wait_for_test_initialized(sim: SimServer, timeout_frames: int) -> dict[str, int]:
    last_status: dict[str, int] | None = None
    for frame in range(timeout_frames + 1):
        status = read_status(sim)
        last_status = status
        if frame == 0 or (frame % 30) == 0:
            log(f"waiting for test init: {status_summary(status)}")
        if status["magic"] == TEST_STATUS_MAGIC:
            if status["verify_errors"] != 0:
                raise RuntimeError(f"z80_sound_test verify failed: {status}")
            if status["initialized"] != 0:
                log(f"test initialized: {status_summary(status)}")
                return status
        sim.call("sim.run_frames", {"count": 1})
    raise RuntimeError(f"timed out waiting for z80_sound_test initialization; last={last_status}")


def wait_for_gui(sim: SimServer, timeout_frames: int) -> dict[str, Any]:
    required = {"WAVE", "REPEAT", "DELAY", "START"}
    last_state: dict[str, Any] | None = None
    for frame in range(timeout_frames + 1):
        state = sim.call("gui.get_state")
        last_state = state
        labels = {str(entry.get("label", "")).upper() for entry in state.get("entries", [])} if state.get("available") else set()
        if frame == 0 or (frame % 30) == 0:
            log(f"waiting for GUI: available={state.get('available')} labels={sorted(labels)}")
        if required.issubset(labels):
            log("GUI ready")
            return state
        sim.call("sim.run_frames", {"count": 1})
    raise RuntimeError(f"timed out waiting for z80_sound_test GUI; last={last_state}")


def run_make(args: list[str]) -> None:
    print("+", " ".join(args), flush=True)
    subprocess.run(args, cwd=REPO_ROOT, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Capture z80_sound_test simulator audio")
    parser.add_argument("--wave", type=int, required=True, help="Wave index to play")
    parser.add_argument("--repeat", type=int, required=True, help="Number of times to play the wave")
    parser.add_argument("--delay", type=int, required=True, help="Frame delay between repeats")
    parser.add_argument("--frames", type=int, required=True, help="Frames to run after pressing Start")
    parser.add_argument(
        "--output",
        default="audio_captures/z80_sound_test_capture.bin",
        help="Output binary audio packet stream path",
    )
    parser.add_argument(
        "--wav",
        help="Optional output WAV path. Defaults to output path with .wav suffix unless --no-wav is set.",
    )
    parser.add_argument("--no-wav", action="store_true", help="Do not decode the captured packet stream to WAV")
    parser.add_argument("--sample-rate", type=int, help="Optional WAV sample rate override passed to capture_stream.py")
    parser.add_argument("--strict", action="store_true", help="Pass --strict to capture_stream.py")
    parser.add_argument("--target", default="pgm_test", help="Test ROM target to build/load")
    parser.add_argument("--page", default="z80_sound_test", help="Test ROM page to build")
    parser.add_argument("--sim", default=str(SIM_DIR / "sim"), help="Simulator executable path")
    parser.add_argument("--rom-dir", help="Value for PGM_ROM_DIR while running the simulator")
    parser.add_argument("--init-timeout-frames", type=int, default=300, help="Frames to wait for test init/GUI")
    parser.add_argument("--no-build", action="store_true", help="Do not build the test ROM first")
    parser.add_argument("--build-sim", action="store_true", help="Build the simulator before running")
    args = parser.parse_args()

    for name in ("wave", "repeat", "delay", "frames"):
        if getattr(args, name) < 0:
            parser.error(f"--{name} must be non-negative")
    if args.repeat == 0:
        parser.error("--repeat must be at least 1")

    log("starting z80_sound_test capture workflow")
    log(f"requested wave={args.wave} repeat={args.repeat} delay={args.delay} frames={args.frames}")

    if args.build_sim:
        log("building simulator")
        run_make(["make", "-C", "sim", "sim"])

    if not args.no_build:
        log(f"building test ROM target={args.target} page={args.page}")
        run_make(["make", "-j8", "-C", "testroms", f"TARGET={args.target}", f"PAGE={args.page}"])

    sim_path = Path(args.sim)
    if not sim_path.is_absolute():
        sim_path = (REPO_ROOT / sim_path).resolve()
    if not sim_path.exists():
        raise SystemExit(f"simulator not found: {sim_path}")

    output_path = Path(args.output)
    if not output_path.is_absolute():
        output_path = (REPO_ROOT / output_path).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    env = os.environ.copy()
    if args.rom_dir:
        env["PGM_ROM_DIR"] = args.rom_dir

    log(f"starting simulator server: {sim_path}")
    sim = SimServer(sim_path, env=env)
    capture_started = False
    try:
        log("initializing simulator")
        sim.call("sim.initialize", {"headless": True})
        log(f"loading game: {args.target}")
        sim.call("sim.load_game", {"name": args.target})
        log("resetting simulator")
        sim.call("sim.reset", {"cycles": 100})

        log("waiting for z80_sound_test to initialize")
        init_status = wait_for_test_initialized(sim, args.init_timeout_frames)
        log("waiting for exported TestROM GUI")
        wait_for_gui(sim, args.init_timeout_frames)

        log("setting GUI values")
        sim.call("gui.set_override", {"index": 0, "value": args.wave & 0xFFFF})
        log(f"  Wave={args.wave}")
        sim.call("gui.set_override", {"index": 1, "value": args.repeat & 0xFFFF})
        log(f"  Repeat={args.repeat}")
        sim.call("gui.set_override", {"index": 2, "value": args.delay & 0xFFFF})
        log(f"  Delay={args.delay}")
        # Let the test ROM consume the overrides and publish a fresh safe GUI snapshot.
        sim.call("sim.run_frames", {"count": 1})
        log(f"status after GUI setup: {status_summary(read_status(sim))}")

        log(f"starting audio capture: {output_path}")
        sim.call("audio_capture.start", {"filename": str(output_path)})
        capture_started = True
        log("pressing Start button")
        sim.call("gui.press_button", {"index": 3})
        log(f"running {args.frames} frames")
        run_result = sim.call("sim.run_frames", {"count": args.frames})
        log(f"run complete: {run_result}")
        log("stopping audio capture")
        sim.call("audio_capture.stop")
        capture_started = False
        final_status = read_status(sim)
        log(f"final test status: {status_summary(final_status)}")
        log("shutting down simulator")
        sim.call("sim.shutdown")

        wav_path = None
        decode_summary_path = None
        if not args.no_wav:
            wav_path = Path(args.wav) if args.wav else output_path.with_suffix(".wav")
            if not wav_path.is_absolute():
                wav_path = (REPO_ROOT / wav_path).resolve()
            wav_path.parent.mkdir(parents=True, exist_ok=True)
            decode_summary_path = wav_path.with_suffix(wav_path.suffix + ".summary.json")

            log(f"decoding WAV: {wav_path}")
            capture_stream = REPO_ROOT / "util" / "capture_stream.py"
            decode_cmd = [
                sys.executable,
                str(capture_stream),
                str(wav_path),
                "--input",
                str(output_path),
                "--summary-json",
                str(decode_summary_path),
            ]
            if args.sample_rate:
                decode_cmd += ["--sample-rate", str(args.sample_rate)]
            if args.strict:
                decode_cmd.append("--strict")
            run_make(decode_cmd)
            log(f"WAV decode complete: {wav_path}")

        summary = {
            "output": str(output_path),
            "bytes": output_path.stat().st_size,
            "wav": str(wav_path) if wav_path else None,
            "wav_summary": str(decode_summary_path) if decode_summary_path else None,
            "initial_status": init_status,
            "final_status": final_status,
            "run_result": run_result,
        }
        print(json.dumps(summary, indent=2))
        return 0
    finally:
        if capture_started:
            try:
                sim.call("audio_capture.stop")
            except Exception:
                pass
        sim.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
