#!/usr/bin/env python3
"""Thin serial capture helper for ESP32-P4 CTest integration.

Single responsibility: open serial port, stream output to log file + stdout,
detect stop patterns (doctest pass/fail/crash), exit with appropriate code.

Exit codes:
  0 = Tests passed ([doctest] Status: SUCCESS!)
  1 = Tests failed ([doctest] Status: FAILURE!)
  2 = Timeout (boot or test phase)
  3 = Device crashed (Guru Meditation, abort, Backtrace)
  4 = Serial port error (busy, not found)

Called by cmake/esp32p4_flash_and_test.cmake via execute_process().

SPDX-License-Identifier: MIT
"""

import argparse
import re
import sys
import time


def main():
    parser = argparse.ArgumentParser(description="ESP32-P4 serial test capture")
    parser.add_argument("--port", required=True, help="Serial port (e.g. COM5)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--boot-timeout", type=int, default=15,
                        help="Seconds to wait for boot marker")
    parser.add_argument("--test-timeout", type=int, default=120,
                        help="Seconds to wait for test completion")
    parser.add_argument("--log-file", required=True, help="Output log file path")
    parser.add_argument("--boot-marker", default="Sub0h264 unit tests starting...",
                        help="String that indicates device has booted")
    parser.add_argument("--success-pattern", default="[doctest] Status: SUCCESS!")
    parser.add_argument("--failure-pattern", default="[doctest] Status: FAILURE!")
    parser.add_argument("--crash-patterns", nargs="*",
                        default=["Guru Meditation Error", "abort() was called",
                                 "Backtrace:"])
    args = parser.parse_args()

    try:
        import serial
    except ImportError:
        print("ERROR: pyserial not installed. Run: pip install pyserial",
              file=sys.stderr)
        sys.exit(4)

    # ── Open serial with DTR/RTS set BEFORE open ────────────────────────
    try:
        ser = serial.Serial()
        ser.port = args.port
        ser.baudrate = args.baud
        ser.timeout = 0.1  # 100ms read timeout for polling
        ser.dtr = False
        ser.rts = False
        ser.open()
        ser.reset_input_buffer()  # Flush stale data
    except serial.SerialException as e:
        print(f"ERROR: Cannot open {args.port}: {e}", file=sys.stderr)
        sys.exit(4)

    ansi_re = re.compile(r"\x1b\[[0-9;]*[a-zA-Z]")
    accumulated = ""
    result = None  # None=pending, 0=pass, 1=fail, 3=crash

    try:
        log_fh = open(args.log_file, "w", encoding="utf-8", errors="replace")
    except OSError as e:
        print(f"ERROR: Cannot create log file {args.log_file}: {e}",
              file=sys.stderr)
        ser.close()
        sys.exit(4)

    def read_and_log():
        nonlocal accumulated
        try:
            raw = ser.read(4096)
        except serial.SerialException:
            return False
        if not raw:
            return True
        text = raw.decode("utf-8", errors="replace")
        clean = ansi_re.sub("", text)
        log_fh.write(clean)
        log_fh.flush()
        sys.stdout.write(clean)
        sys.stdout.flush()
        accumulated += clean
        return True

    # ── Phase 1: Wait for boot marker ───────────────────────────────────
    print(f"[serial_capture] Waiting for boot marker (timeout {args.boot_timeout}s)...",
          flush=True)
    boot_deadline = time.monotonic() + args.boot_timeout
    booted = False

    while time.monotonic() < boot_deadline:
        if not read_and_log():
            break
        if args.boot_marker in accumulated:
            booted = True
            print("[serial_capture] Boot marker detected.", flush=True)
            break

    if not booted:
        print(f"[serial_capture] TIMEOUT: boot marker not seen in {args.boot_timeout}s",
              file=sys.stderr, flush=True)
        log_fh.close()
        ser.close()
        sys.exit(2)

    # ── Phase 2: Capture test output ────────────────────────────────────
    print(f"[serial_capture] Capturing test output (timeout {args.test_timeout}s)...",
          flush=True)
    test_deadline = time.monotonic() + args.test_timeout
    terminal_found = False

    while time.monotonic() < test_deadline:
        if not read_and_log():
            break

        # Check for crash patterns (highest priority)
        for cp in args.crash_patterns:
            if cp in accumulated:
                result = 3

        # Check for doctest results (last-match-wins: crash overrides)
        if args.failure_pattern in accumulated and result != 3:
            result = 1
            terminal_found = True
        if args.success_pattern in accumulated and result != 3:
            result = 0
            terminal_found = True

        # If we found a terminal pattern, do a 2s post-flush to catch late crashes
        if terminal_found:
            flush_deadline = time.monotonic() + 2.0
            while time.monotonic() < flush_deadline:
                read_and_log()
                # Re-check crash patterns during flush
                for cp in args.crash_patterns:
                    if cp in accumulated:
                        result = 3
            break
    else:
        # Loop exhausted = timeout
        if result is None:
            print(f"[serial_capture] TIMEOUT: tests did not complete in {args.test_timeout}s",
                  file=sys.stderr, flush=True)
            result = 2

    # ── Cleanup ─────────────────────────────────────────────────────────
    ser.close()
    log_fh.close()

    exit_code = result if result is not None else 2
    labels = {0: "PASSED", 1: "FAILED", 2: "TIMEOUT", 3: "CRASHED"}
    print(f"[serial_capture] Result: {labels.get(exit_code, 'UNKNOWN')} (exit {exit_code})",
          flush=True)
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
