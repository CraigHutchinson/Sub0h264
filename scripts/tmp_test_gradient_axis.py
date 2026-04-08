#!/usr/bin/env python3
"""Test CAVLC and CABAC gradient fixtures, compare against ffmpeg per-axis."""
import subprocess, os, math, glob

FIXTURES = "tests/fixtures"

def ffmpeg_y(h264, w, h):
    r = subprocess.run(["ffmpeg", "-i", h264, "-vframes", "1", "-pix_fmt", "yuv420p",
                        "-f", "rawvideo", "pipe:"], capture_output=True, timeout=10)
    return r.stdout[:w*h] if r.returncode == 0 else None

def psnr(a, b):
    if not a or not b or len(a) != len(b): return -1
    mse = sum((a[i]-b[i])**2 for i in range(len(a))) / len(a)
    return 99.0 if mse < 0.001 else 10*math.log10(255*255/mse)

def max_diff(a, b):
    if not a or not b or len(a) != len(b): return -1
    return max(abs(a[i]-b[i]) for i in range(len(a)))

def get_dims(h264):
    r = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0",
                        "-show_entries", "stream=width,height", "-of", "csv=p=0",
                        h264], capture_output=True, text=True, timeout=5)
    if r.returncode == 0:
        p = r.stdout.strip().split(',')
        return int(p[0]), int(p[1])
    return None, None

# Collect all gradient fixtures
grad_fixtures = sorted(glob.glob(os.path.join(FIXTURES, "*_1mb_grad_*.h264")))
grad_fixtures += sorted(glob.glob(os.path.join(FIXTURES, "*_4mb_grad_*.h264")))

print(f"{'Fixture':<40} {'ff PSNR':>8} {'maxdiff':>8} {'px[0]ff':>8} {'px[0]raw':>8}")
print("-" * 76)

for h264 in sorted(grad_fixtures):
    name = os.path.basename(h264).replace('.h264','')
    raw = h264.replace('.h264','_raw.yuv')
    if not os.path.exists(raw): continue

    w, h = get_dims(h264)
    if not w: continue

    raw_y = open(raw,'rb').read()[:w*h]
    ff_y = ffmpeg_y(h264, w, h)
    if not ff_y: continue

    p = psnr(raw_y, ff_y)
    md = max_diff(raw_y, ff_y)
    px0_ff = ff_y[0]
    px0_raw = raw_y[0]

    pstr = f"{p:.1f}" if p > 0 else "ERR"
    print(f"{name:<40} {pstr:>8} max={md:>3}  ff={px0_ff:>3}  raw={px0_raw:>3}")
