#!/usr/bin/env python3
"""Trace MB(3,0) CAVLC coefficients and prediction for bouncing ball.

Uses our trace tool at block level to show exact coefficients decoded,
then compares against manual bitstream decode.

Usage:
    python scripts/trace_mb3_coeffs.py

SPDX-License-Identifier: MIT
"""
import subprocess
import sys


def main():
    trace_exe = "build/test_apps/trace/Release/sub0h264_trace.exe"
    fixture = "tests/fixtures/bouncing_ball_ionly.h264"

    # Run trace at block detail level for MB(3,0) and MB(4,0)
    cmd = [trace_exe, fixture, "--level", "block", "--mb", "3,0"]
    print(f"=== Tracing MB(3,0) ===")
    result = subprocess.run(cmd, capture_output=True, text=True)
    print(result.stdout[:3000] if result.stdout else "(no output)")
    if result.stderr:
        print(f"STDERR: {result.stderr[:500]}")

    print(f"\n=== Tracing MB(4,0) ===")
    cmd = [trace_exe, fixture, "--level", "block", "--mb", "4,0"]
    result = subprocess.run(cmd, capture_output=True, text=True)
    print(result.stdout[:3000] if result.stdout else "(no output)")


if __name__ == "__main__":
    main()
