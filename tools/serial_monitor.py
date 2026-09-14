"""Reconnectable monitor for KLQ's USB CDC angle stream."""
from __future__ import annotations

import argparse
import time
from datetime import datetime

import serial
from serial.tools import list_ports

KLQ_VID = 0x314B
KLQ_PIDS = {0x0108, 0x0109}


def find_port(explicit: str | None) -> str | None:
    if explicit:
        return explicit
    matches = [p.device for p in list_ports.comports()
               if p.vid == KLQ_VID and p.pid in KLQ_PIDS]
    return matches[0] if len(matches) == 1 else None


def main() -> None:
    parser = argparse.ArgumentParser(description="Monitor KLQ USB CDC output and reconnect automatically")
    parser.add_argument("--port", help="COM port; omit to detect KLQ VID/PID")
    parser.add_argument("--baud", type=int, default=115200,
                        help="CDC line coding shown to Windows (default: 115200)")
    args = parser.parse_args()
    last_state = ""

    print("KLQ USB serial monitor (Ctrl+C to stop)", flush=True)
    while True:
        port = find_port(args.port)
        if not port:
            if last_state != "waiting":
                print("Waiting for KLQ USB CDC...", flush=True)
                last_state = "waiting"
            time.sleep(0.5)
            continue
        try:
            with serial.Serial(port, args.baud, timeout=0.5) as device:
                print(f"Connected: {port}", flush=True)
                last_state = "connected"
                while True:
                    line = device.readline()
                    if line:
                        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                        print(f"[{timestamp}] {line.decode('ascii', errors='replace').rstrip()}", flush=True)
        except (serial.SerialException, OSError) as error:
            if last_state != "disconnected":
                print(f"Disconnected: {error}; retrying...", flush=True)
                last_state = "disconnected"
            time.sleep(0.5)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nMonitor stopped.")
