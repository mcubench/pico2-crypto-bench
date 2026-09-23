#!/usr/bin/env python3
"""Bounded USB CDC capture with simple firmware result validation."""

from __future__ import annotations

import argparse
import glob
import sys
import time

import serial


def find_port(explicit: str | None, deadline: float) -> str:
    while time.monotonic() < deadline:
        candidates = sorted(glob.glob("/dev/serial/by-id/usb-Raspberry_Pi_Pico*"))
        if explicit:
            return explicit
        if len(candidates) == 1:
            return candidates[0]
        if len(candidates) > 1:
            raise RuntimeError(f"expected one Pico serial device, found {candidates}")
        time.sleep(0.2)
    raise TimeoutError("Pico USB serial device did not appear")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--seconds", type=float, default=8.0)
    parser.add_argument("--port")
    parser.add_argument("--expect", action="append", default=[])
    parser.add_argument("--wait-for-port", type=float, default=15.0)
    args = parser.parse_args()
    if args.seconds <= 0 or args.wait_for_port <= 0:
        parser.error("timeouts must be positive")

    port_deadline = time.monotonic() + args.wait_for_port
    port_name = find_port(args.port, port_deadline)
    deadline = time.monotonic() + args.seconds
    saw_output = False
    matched = not args.expect
    fatal_prefixes = ("FAULT", "TEST:FAIL")
    fatal_fragments = ("setup failed", " signature did not verify", "  failed")

    with serial.Serial(port_name, 115200, timeout=0.2) as device:
        while time.monotonic() < deadline:
            raw = device.readline()
            if not raw:
                continue
            saw_output = True
            line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            print(line, flush=True)
            if line.startswith(fatal_prefixes) or any(text in line for text in fatal_fragments):
                print(f"MONITOR:FAIL reason=firmware line={line!r}", file=sys.stderr)
                return 1
            if any(marker in line for marker in args.expect):
                matched = True
                break

    if not saw_output:
        print("MONITOR:FAIL reason=no_serial_output", file=sys.stderr)
        return 1
    if not matched:
        print(f"MONITOR:FAIL reason=timeout expected={args.expect!r}", file=sys.stderr)
        return 1
    print(f"MONITOR:PASS port={port_name} expected={args.expect!r}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, TimeoutError, serial.SerialException) as exc:
        print(f"MONITOR:FAIL reason={exc}", file=sys.stderr)
        raise SystemExit(1)

