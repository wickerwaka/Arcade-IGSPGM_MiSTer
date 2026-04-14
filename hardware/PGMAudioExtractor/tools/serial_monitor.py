#!/usr/bin/env /usr/bin/python3
import glob
import os
import sys
import time

import serial

BAUD = int(os.environ.get("PGM_SERIAL_BAUD", "115200"))
POLL_S = float(os.environ.get("PGM_SERIAL_POLL_S", "0.5"))
READ_SIZE = int(os.environ.get("PGM_SERIAL_READ_SIZE", "512"))


def find_ports():
    return sorted(glob.glob('/dev/cu.usbmodem*')) + sorted(glob.glob('/dev/tty.usbmodem*'))


def log(msg):
    print(msg, flush=True)


def open_port(port):
    log(f"TRY_OPEN {port}")
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = BAUD
    ser.timeout = POLL_S
    ser.rtscts = False
    ser.dsrdtr = False
    ser.xonxoff = False
    ser.open()
    ser.dtr = True
    ser.rts = False
    return ser


def monitor_port(port):
    try:
        ser = open_port(port)
    except (serial.SerialException, OSError) as e:
        log(f"SERIAL_BUSY {port} {e}")
        return

    log(f"OPEN {port} baud={BAUD}")
    try:
        while True:
            data = ser.read(READ_SIZE)
            if data:
                sys.stdout.buffer.write(data.replace(b'\r', b'\n'))
                sys.stdout.flush()

            if not os.path.exists(port):
                log(f"DISCONNECT {port}")
                break
    except (serial.SerialException, OSError) as e:
        log(f"SERIAL_ERROR {port} {e}")
    finally:
        try:
            ser.close()
        except Exception:
            pass


def main():
    log("SERIAL_MONITOR_START")
    last_missing_report = 0.0
    while True:
        ports = find_ports()
        if not ports:
            now = time.time()
            if now - last_missing_report >= 2.0:
                log("WAITING_FOR_SERIAL")
                last_missing_report = now
            time.sleep(POLL_S)
            continue

        log("PORTS " + " ".join(ports))
        monitor_port(ports[0])
        time.sleep(POLL_S)


if __name__ == "__main__":
    main()
