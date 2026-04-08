#!/usr/bin/env python3
"""Test all micro CABAC fixtures: ffmpeg ground truth + per-pixel analysis."""
import subprocess, os, math, glob

FIXTURES = "tests/fixtures"

def decode_ffmpeg(h264, w, h):
    r = subprocess.run(["ffmpeg", "-i", h264, "-vframes", "1", "-pix_fmt", "yuv420p",
                        "-f", "rawvideo", "pipe:"], capture_output=True, timeout=10)
    return r.stdout[:w*h] if r.returncode == 0 else None

def psnr(a, b):
    if not a or not b or len(a) != len(b): return -1
    mse = sum((a[i]-b[i])**2 for i in range(len(a))) / len(a)
    return 99.0 if mse < 0.001 else 10*math.log10(255*255/mse)

# Detect dimensions from SPS
def get_dims(h264):
    """Quick dimension parse from SPS."""
    data = open(h264, 'rb').read()
    # Use ffprobe
    r = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0",
                        "-show_entries", "stream=width,height",
                        "-of", "csv=p=0", h264], capture_output=True, text=True, timeout=5)
    if r.returncode == 0:
        parts = r.stdout.strip().split(',')
        return int(parts[0]), int(parts[1])
    return None, None

# Test all cabac_1mb_* and cabac_*mb_* fixtures
fixtures = sorted(glob.glob(os.path.join(FIXTURES, "cabac_*mb_*.h264")))

print(f"{'Fixture':<35} {'Size':>7} {'ffmpeg':>8} {'pixel[0]':>8} {'raw[0]':>6}")
print("-" * 70)

for h264 in fixtures:
    name = os.path.basename(h264).replace('.h264','')
    raw_path = h264.replace('.h264', '_raw.yuv')
    if not os.path.exists(raw_path): continue

    w, h = get_dims(h264)
    if not w: continue

    raw_y = open(raw_path, 'rb').read()[:w*h]
    ff_y = decode_ffmpeg(h264, w, h)
    p = psnr(raw_y, ff_y)
    ff_px0 = ff_y[0] if ff_y else -1
    raw_px0 = raw_y[0] if raw_y else -1
    pstr = f"{p:.1f}" if p > 0 else "FAIL"

    print(f"{name:<35} {w}x{h:>3} {pstr:>7}  px0={ff_px0:>3}  raw={raw_px0:>3}")
