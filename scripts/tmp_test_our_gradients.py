#!/usr/bin/env python3
"""Test OUR decoder on all gradient fixtures. Compares CAVLC vs ffmpeg to find rounding axis."""
import subprocess, os, math, glob

FIXTURES = "tests/fixtures"
# Use the C++ test binary to decode
# We'll parse the test output for PSNR values

# Actually, let's compute PSNR via the test framework output.
# For now, use a simpler approach: compile and run a one-off test.

# Let's just read the raw files and compare pixel-by-pixel with ffmpeg
def ffmpeg_y(h264, w, h):
    r = subprocess.run(["ffmpeg", "-i", h264, "-vframes", "1", "-pix_fmt", "yuv420p",
                        "-f", "rawvideo", "pipe:"], capture_output=True, timeout=10)
    return r.stdout[:w*h] if r.returncode == 0 else None

def psnr(a, b):
    if not a or not b or len(a) != len(b): return -1
    mse = sum((a[i]-b[i])**2 for i in range(len(a))) / len(a)
    return 99.0 if mse < 0.001 else 10*math.log10(255*255/mse)

def get_dims(h264):
    r = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0",
                        "-show_entries", "stream=width,height", "-of", "csv=p=0",
                        h264], capture_output=True, text=True, timeout=5)
    if r.returncode == 0:
        p = r.stdout.strip().split(',')
        return int(p[0]), int(p[1])
    return None, None

# Focus on CAVLC 1MB gradients
print("=== CAVLC gradient axis analysis (our decoder vs ffmpeg) ===")
print(f"{'Axis':<25} {'ffmpeg':>7} {'ours':>7} {'gap':>7} {'max_px_diff':>12}")
print("-" * 62)

# We need to decode with our decoder. Since the trace tool doesn't work for small sizes,
# let's check what the C++ test framework reported. Actually let me just run the
# specific test case.

exe = "build/tests/Release/Sub0h264_Tests.exe"

# Run a custom diagnostic
import struct

# Parse the gradient fixtures through our decoder using a trick:
# Decode each fixture by importing the H264Decoder... nah, we need C++.
# Let's just run the test binary with specific test cases.

result = subprocess.run([exe, "-tc=CAVLC small*", "-s"], capture_output=True, text=True, timeout=30)
# Extract PSNR values
for line in result.stdout.split('\n'):
    if 'ours=' in line:
        print(line.strip())

print("\nNote: Run the C++ test 'CABAC vs CAVLC gradient axis' for detailed results")
print("(needs to be added to test_cabac_small.cpp)")
