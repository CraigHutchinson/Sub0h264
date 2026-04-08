#!/usr/bin/env python3
"""Test ffmpeg PSNR on all small CABAC fixtures to establish the ground truth.
Our C++ test will separately test the main decoder."""
import subprocess
import os
import math

FIXTURES = "tests/fixtures"

sizes = {
    "1mb": (16, 16), "2mb_h": (32, 16), "2mb_v": (16, 32),
    "4mb": (32, 32), "16mb": (64, 64),
}
contents = ["flat", "gradient", "noisy", "checkerboard"]

def psnr_bytes(a, b):
    if len(a) != len(b) or len(a) == 0: return -1
    mse = sum((a[i] - b[i])**2 for i in range(len(a))) / len(a)
    if mse < 0.001: return 99.0
    return 10 * math.log10(255*255 / mse)

print(f"{'Fixture':<40} {'Size':>7} {'MBs':>4} {'ffmpeg Y PSNR':>14} {'H.264 bytes':>12}")
print("-" * 82)

for size_name, (w, h) in sizes.items():
    n_mbs = (w // 16) * (h // 16)
    for content in contents:
        name = f"cabac_{size_name}_{content}"
        h264 = os.path.join(FIXTURES, f"{name}.h264")
        raw = os.path.join(FIXTURES, f"{name}_raw.yuv")
        if not os.path.exists(h264): continue

        raw_y = open(raw, 'rb').read()[:w*h]
        result = subprocess.run(
            ["ffmpeg", "-i", h264, "-vframes", "1", "-pix_fmt", "yuv420p",
             "-f", "rawvideo", "pipe:"],
            capture_output=True, timeout=10)
        if result.returncode != 0:
            print(f"{name:<40} {w}x{h:>3} {n_mbs:>4} {'FAIL':>14} {os.path.getsize(h264):>12,}")
            continue
        ffmpeg_y = result.stdout[:w*h]
        p = psnr_bytes(raw_y, ffmpeg_y)
        pstr = f"{p:.1f} dB" if p > 0 else "FAIL"
        print(f"{name:<40} {w}x{h:>3} {n_mbs:>4} {pstr:>14} {os.path.getsize(h264):>12,}")
