#!/usr/bin/env python3
"""Test the spec-ref decoder on all available CABAC fixtures by
running SpecRef_Tests with the decode test case and parsing output."""
import subprocess
import sys

exe = "build/reference_decoder/Release/SpecRef_Tests.exe"
result = subprocess.run([exe, "-s"], capture_output=True, text=True, timeout=60)
print(result.stdout[-3000:] if len(result.stdout) > 3000 else result.stdout)
if result.returncode != 0:
    print("STDERR:", result.stderr[-1000:])
    sys.exit(result.returncode)
