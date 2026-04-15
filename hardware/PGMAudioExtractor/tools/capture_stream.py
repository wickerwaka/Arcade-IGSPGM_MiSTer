#!/usr/bin/env /usr/bin/python3
import argparse
import collections
import glob
import json
import signal
import struct
import sys
import time
import wave
from pathlib import Path

import serial

MAGIC = 0x414D4750
HDR_FMT = "<IHHIIQIQII"
HDR_SIZE = struct.calcsize(HDR_FMT)
TYPE_AUDIO = 1
TYPE_STATUS = 2
STATUS_FMT = "<IIIIIIIIIIIIIIhhIIII"
STATUS_SIZE = struct.calcsize(STATUS_FMT)
POLL_S = 0.2
READ_SIZE = 4096

stop_requested = False


def on_signal(signum, frame):
    del signum, frame
    global stop_requested
    stop_requested = True


def find_ports():
    return sorted(glob.glob('/dev/cu.usbmodem*')) + sorted(glob.glob('/dev/tty.usbmodem*'))


def choose_port(explicit):
    if explicit:
        return explicit
    ports = find_ports()
    return ports[0] if ports else None


def open_port(port):
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = POLL_S
    ser.rtscts = False
    ser.dsrdtr = False
    ser.xonxoff = False
    ser.open()
    ser.dtr = True
    ser.rts = False
    return ser


def sync_to_magic(ser, buf):
    magic = struct.pack('<I', MAGIC)
    while True:
        idx = buf.find(magic)
        if idx >= 0:
            if idx:
                del buf[:idx]
            return True
        data = ser.read(READ_SIZE)
        if not data:
            if stop_requested:
                return False
            continue
        buf.extend(data)


def read_exact(ser, buf, size):
    while len(buf) < size:
        data = ser.read(READ_SIZE)
        if not data:
            if stop_requested:
                return None
            continue
        buf.extend(data)
    out = bytes(buf[:size])
    del buf[:size]
    return out


def decode_header(data):
    fields = struct.unpack(HDR_FMT, data)
    return {
        'magic': fields[0],
        'version': fields[1],
        'type': fields[2],
        'payload_bytes': fields[3],
        'block_seq': fields[4],
        'frame_start': fields[5],
        'frame_count': fields[6],
        't_us': fields[7],
        'raw_lrclk_hz': fields[8],
        'flags': fields[9],
    }


def decode_status(data):
    fields = struct.unpack(STATUS_FMT, data)
    keys = [
        'uptime_ms', 'rate_hz', 'raw_rate_hz', 'edge_count', 'elapsed_us', 'idle_us',
        'rate_status', 'ready_mask', 'processed_dma_blocks', 'dropped_dma_blocks',
        'dropped_audio_frames', 'channel_word_count', 'stereo_frame_count',
        'nonzero_sample_count', 'last_left_sample', 'last_right_sample',
        'stream_dropped_packets', 'stream_dropped_bytes', 'stream_queue_depth', 'reserved'
    ]
    return dict(zip(keys, fields))


def final_sample_rate(counter, fallback):
    if not counter:
        return fallback
    return counter.most_common(1)[0][0]


def build_wav(raw_path, wav_path, sample_rate, frame_count):
    with raw_path.open('rb') as src, wave.open(str(wav_path), 'wb') as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        remaining = frame_count * 4
        while remaining > 0:
            chunk = src.read(min(65536, remaining))
            if not chunk:
                break
            wav.writeframes(chunk)
            remaining -= len(chunk)


def main():
    parser = argparse.ArgumentParser(description='Capture native PGM audio stream over CDC')
    parser.add_argument('output', help='Output WAV path')
    parser.add_argument('--port', help='Serial port to use')
    parser.add_argument('--duration', type=float, default=10.0, help='Capture duration in seconds (default: 10)')
    parser.add_argument('--sample-rate', type=int, help='Override WAV sample rate metadata')
    parser.add_argument('--summary-json', help='Optional path for summary JSON output')
    parser.add_argument('--strict', action='store_true', help='Exit non-zero if packet/frame gaps are detected')
    args = parser.parse_args()

    output_path = Path(args.output)
    raw_path = output_path.with_suffix(output_path.suffix + '.raw')
    jsonl_path = output_path.with_suffix(output_path.suffix + '.jsonl')
    summary_path = Path(args.summary_json) if args.summary_json else output_path.with_suffix(output_path.suffix + '.summary.json')
    output_path.parent.mkdir(parents=True, exist_ok=True)

    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)

    port = choose_port(args.port)
    if not port:
        print('No serial port found', file=sys.stderr)
        return 1

    print(f'Opening {port}', flush=True)
    ser = open_port(port)
    buf = bytearray()
    rate_counter = collections.Counter()
    total_frames = 0
    audio_packets = 0
    status_packets = 0
    malformed_status_packets = 0
    first_packet_time = None
    first_audio_time = None
    deadline = time.time() + args.duration if args.duration > 0 else None
    expected_block_seq = None
    expected_frame_start = None
    block_gap_count = 0
    frame_gap_count = 0
    block_gap_frames = 0
    status_drop_reports = 0
    max_queue_depth = 0
    latest_status = None

    with raw_path.open('wb') as raw_file, jsonl_path.open('w', encoding='utf-8') as jsonl:
        try:
            while not stop_requested:
                if deadline is not None and time.time() >= deadline:
                    break
                if not sync_to_magic(ser, buf):
                    break
                hdr_data = read_exact(ser, buf, HDR_SIZE)
                if hdr_data is None:
                    break
                hdr = decode_header(hdr_data)
                if hdr['magic'] != MAGIC:
                    continue
                payload = read_exact(ser, buf, hdr['payload_bytes'])
                if payload is None:
                    break

                now = time.time()
                if first_packet_time is None:
                    first_packet_time = now

                record = {'header': hdr}
                if expected_block_seq is not None and hdr['block_seq'] != expected_block_seq:
                    block_gap_count += 1
                    record['block_seq_gap'] = [expected_block_seq, hdr['block_seq']]
                expected_block_seq = hdr['block_seq'] + 1

                if hdr['type'] == TYPE_AUDIO:
                    if first_audio_time is None:
                        first_audio_time = now
                    audio_packets += 1
                    raw_file.write(payload)
                    total_frames += hdr['frame_count']
                    if hdr['raw_lrclk_hz']:
                        rate_counter[hdr['raw_lrclk_hz']] += hdr['frame_count']
                    if expected_frame_start is not None and hdr['frame_start'] != expected_frame_start:
                        frame_gap_count += 1
                        gap = hdr['frame_start'] - expected_frame_start
                        if gap > 0:
                            block_gap_frames += gap
                        record['frame_gap'] = [expected_frame_start, hdr['frame_start']]
                    expected_frame_start = hdr['frame_start'] + hdr['frame_count']
                elif hdr['type'] == TYPE_STATUS:
                    status_packets += 1
                    if len(payload) == STATUS_SIZE:
                        status = decode_status(payload)
                        latest_status = status
                        max_queue_depth = max(max_queue_depth, status['stream_queue_depth'])
                        if status['stream_dropped_packets']:
                            status_drop_reports += 1
                        record['status'] = status
                    else:
                        malformed_status_packets += 1
                        record['status_error'] = f'unexpected status payload {len(payload)}'
                else:
                    record['unknown_payload_bytes'] = len(payload)

                jsonl.write(json.dumps(record) + '\n')

                if (audio_packets + status_packets) % 50 == 0:
                    ref = first_audio_time or first_packet_time or now
                    elapsed = max(now - ref, 0.001)
                    print(
                        f'packets={audio_packets + status_packets} '
                        f'audio={audio_packets} status={status_packets} '
                        f'frames={total_frames} rate~={final_sample_rate(rate_counter, 33074)} '
                        f'fps={total_frames/elapsed:.1f} gaps={frame_gap_count}/{block_gap_count}',
                        flush=True,
                    )
        finally:
            ser.close()

    sample_rate = args.sample_rate or final_sample_rate(rate_counter, 33074)
    build_wav(raw_path, output_path, sample_rate, total_frames)

    summary = {
        'port': port,
        'output_wav': str(output_path),
        'output_raw': str(raw_path),
        'output_jsonl': str(jsonl_path),
        'sample_rate': sample_rate,
        'frames': total_frames,
        'audio_packets': audio_packets,
        'status_packets': status_packets,
        'malformed_status_packets': malformed_status_packets,
        'block_gap_count': block_gap_count,
        'frame_gap_count': frame_gap_count,
        'frame_gap_frames': block_gap_frames,
        'status_drop_reports': status_drop_reports,
        'max_queue_depth': max_queue_depth,
        'latest_status': latest_status,
        'rate_histogram': dict(rate_counter),
    }
    summary_path.write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')

    print(
        f'Wrote {output_path} rate={sample_rate} frames={total_frames} '
        f'audio_packets={audio_packets} status_packets={status_packets} '
        f'frame_gaps={frame_gap_count} block_gaps={block_gap_count}',
        flush=True,
    )
    print(f'Metadata: {jsonl_path}', flush=True)
    print(f'Summary: {summary_path}', flush=True)

    if args.strict and (block_gap_count or frame_gap_count or malformed_status_packets):
        return 2
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
