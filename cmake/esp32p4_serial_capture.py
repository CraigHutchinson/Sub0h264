#!/usr/bin/env python3
"""ESP32-P4 serial capture for CTest integration.

Opens serial port, resets device via DTR/RTS, streams output to log file +
stdout, and exits when a terminal condition is detected.

Terminal conditions (checked per-line):
  - Success pattern matched → exit 0
  - Failure pattern matched → exit 1
  - Crash detected (Guru Meditation, abort, Backtrace) → exit 3
  - Device rebooted mid-test (ESP-ROM: seen after boot) → exit 3
  - Timeout → exit 2
  - Serial error → exit 4

Boot detection: any line matching the boot marker (default: "app_main" or
"Calling app_main") switches from boot phase to test phase. The boot phase
has a short timeout; the test phase has a longer one. If boot detection
fails, all output is still captured — the test timeout applies from the
start of capture.

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
                        help="Seconds to wait for boot (soft — capture continues)")
    parser.add_argument("--test-timeout", type=int, default=120,
                        help="Total seconds from start to success/failure")
    parser.add_argument("--log-file", required=True, help="Output log file path")
    parser.add_argument("--boot-marker", default="Calling app_main",
                        help="Substring in any line that indicates boot complete")
    parser.add_argument("--success-pattern", default="[doctest] Status: SUCCESS!",
                        help="Line substring indicating test success")
    parser.add_argument("--failure-pattern", default="[doctest] Status: FAILURE!",
                        help="Line substring indicating test failure")
    parser.add_argument("--crash-patterns", nargs="*",
                        default=["Guru Meditation Error", "abort() was called",
                                 "Backtrace:", "panic'ed"])
    parser.add_argument("--reset", action="store_true", default=True,
                        help="Reset device via DTR/RTS before capture")
    parser.add_argument("--no-reset", action="store_false", dest="reset")
    parser.add_argument("--send-filter", default=None,
                        help="Filter string to send when READY_FOR_FILTER is seen")
    args = parser.parse_args()

    try:
        import serial
    except ImportError:
        print("ERROR: pyserial not installed. Run: pip install pyserial",
              file=sys.stderr)
        sys.exit(4)

    # ── Open serial port ───────────────────────────────────────────────
    # Open in one call — Windows toggles DTR on open, which is expected.
    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except serial.SerialException as e:
        print(f"ERROR: Cannot open {args.port}: {e}", file=sys.stderr)
        sys.exit(4)

    # ── Reset device via DTR/RTS toggle ──────────────────────────────
    if args.reset:
        print("[serial_capture] Resetting device...", flush=True)
        ser.dtr = False
        ser.rts = True      # EN -> LOW (hold in reset)
        time.sleep(0.1)
        ser.rts = False      # EN -> HIGH (release)
        time.sleep(0.1)
        ser.dtr = True
        # Send filter immediately — sits in UART RX buffer until device reads
        if args.send_filter:
            ser.write((args.send_filter + "\n").encode("utf-8"))
            ser.flush()
            print(f"[serial_capture] Sent filter: {args.send_filter}",
                  flush=True)

    ansi_re = re.compile(r"\x1b\[[0-9;]*[a-zA-Z]")
    reboot_re = re.compile(r"ESP-ROM:|rst:0x[0-9a-f]")

    try:
        log_fh = open(args.log_file, "w", encoding="utf-8", errors="replace")
    except OSError as e:
        print(f"ERROR: Cannot create log file {args.log_file}: {e}",
              file=sys.stderr)
        ser.close()
        sys.exit(4)

    # ── Capture loop (line-based) ──────────────────────────────────────
    booted = False
    boot_seen_at = None
    filter_sent = False
    result = None  # None=pending, 0=pass, 1=fail, 2=timeout, 3=crash
    start_time = time.monotonic()
    absolute_deadline = start_time + args.test_timeout
    lines_after_boot = 0

    print(f"[serial_capture] Capturing on {args.port} "
          f"(boot={args.boot_timeout}s, test={args.test_timeout}s)...",
          flush=True)

    try:
        while time.monotonic() < absolute_deadline:
            try:
                raw = ser.readline()
            except serial.SerialException:
                result = 4
                break

            if not raw:
                # No data within timeout — check boot deadline
                if not booted and (time.monotonic() - start_time) > args.boot_timeout:
                    print(f"[serial_capture] WARNING: boot marker not seen in "
                          f"{args.boot_timeout}s (continuing capture)",
                          file=sys.stderr, flush=True)
                    booted = True  # Proceed anyway — don't abort
                    boot_seen_at = time.monotonic()
                continue

            # Decode and clean
            try:
                line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            except Exception:
                continue

            clean = ansi_re.sub("", line)

            # Skip boot noise
            if "invalid header" in clean:
                continue

            # Log and print
            log_fh.write(clean + "\n")
            log_fh.flush()
            print(clean, flush=True)

            # ── Boot detection ─────────────────────────────────────
            if not booted and args.boot_marker in clean:
                booted = True
                boot_seen_at = time.monotonic()
                print("[serial_capture] Boot detected.", flush=True)

            if booted:
                lines_after_boot += 1

            # ── Note filter receipt (for logging only) ────────────
            if not filter_sent and "READY_FOR_FILTER" in clean:
                filter_sent = True

            # ── Reboot detection (crash/watchdog mid-test) ─────────
            if booted and lines_after_boot > 5 and reboot_re.search(clean):
                print("[serial_capture] Device rebooted mid-test!",
                      file=sys.stderr, flush=True)
                result = 3
                break

            # ── Crash detection ────────────────────────────────────
            for cp in args.crash_patterns:
                if cp in clean:
                    print(f"[serial_capture] Crash detected: {cp}",
                          file=sys.stderr, flush=True)
                    result = 3
                    # Read a few more lines for backtrace context
                    flush_end = time.monotonic() + 2.0
                    while time.monotonic() < flush_end:
                        try:
                            extra = ser.readline()
                        except serial.SerialException:
                            break
                        if extra:
                            eline = extra.decode("utf-8", errors="replace").rstrip()
                            eline = ansi_re.sub("", eline)
                            log_fh.write(eline + "\n")
                            print(eline, flush=True)
                    break

            if result is not None:
                break

            # ── Success/failure detection ──────────────────────────
            if args.success_pattern in clean:
                result = 0
                break
            if args.failure_pattern in clean:
                result = 1
                break

    except KeyboardInterrupt:
        print("\n[serial_capture] Interrupted.", flush=True)
        result = 2

    # ── Timeout ────────────────────────────────────────────────────────
    if result is None:
        result = 2
        elapsed = time.monotonic() - start_time
        print(f"[serial_capture] TIMEOUT after {elapsed:.0f}s",
              file=sys.stderr, flush=True)

    # ── Cleanup ────────────────────────────────────────────────────────
    ser.close()
    log_fh.close()

    labels = {0: "PASSED", 1: "FAILED", 2: "TIMEOUT", 3: "CRASHED", 4: "SERIAL_ERROR"}
    print(f"[serial_capture] Result: {labels.get(result, 'UNKNOWN')} (exit {result})",
          flush=True)
    sys.exit(result)


if __name__ == "__main__":
    main()
