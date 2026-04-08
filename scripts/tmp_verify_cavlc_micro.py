#!/usr/bin/env python3
"""Verify our CAVLC decoder matches ffmpeg on all micro fixtures."""
import subprocess, os, math, glob, struct

FIXTURES = "tests/fixtures"
TRACE = "build/test_apps/trace/Release/sub0h264_trace.exe"

def ffmpeg_decode(h264, w, h):
    r = subprocess.run(["ffmpeg", "-i", h264, "-vframes", "1", "-pix_fmt", "yuv420p",
                        "-f", "rawvideo", "pipe:"], capture_output=True, timeout=10)
    return r.stdout[:w*h] if r.returncode == 0 else None

def our_decode(h264, w, h):
    """Decode via trace tool."""
    out = f"build/tmp_verify.yuv"
    r = subprocess.run([TRACE, h264, "--dump", out], capture_output=True, timeout=10)
    if r.returncode != 0 or not os.path.exists(out):
        return None
    data = open(out, 'rb').read()
    os.remove(out)
    return data[:w*h] if len(data) >= w*h else None

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

# Test all CAVLC micro fixtures
fixtures = sorted(glob.glob(os.path.join(FIXTURES, "cavlc_*mb_*.h264")))

print(f"{'Fixture':<35} {'ffmpeg Y':>8} {'ours Y':>8} {'vs ffmpeg':>10} {'px0':>6}")
print("-" * 72)

exact_match = 0
total = 0
for h264 in fixtures:
    name = os.path.basename(h264).replace('.h264','')
    raw = h264.replace('.h264','_raw.yuv')
    if not os.path.exists(raw): continue

    w, h = get_dims(h264)
    if not w: continue

    raw_y = open(raw, 'rb').read()[:w*h]
    ff_y = ffmpeg_decode(h264, w, h)
    our_y = our_decode(h264, w, h)

    ff_psnr = psnr(raw_y, ff_y)
    our_psnr = psnr(raw_y, our_y) if our_y else -1

    # Compare our output against ffmpeg (not raw)
    vs_ff = psnr(ff_y, our_y) if ff_y and our_y else -1

    px0_ours = our_y[0] if our_y else -1
    px0_ff = ff_y[0] if ff_y else -1

    total += 1
    if vs_ff >= 90: exact_match += 1

    vs_str = f"{vs_ff:.1f}" if vs_ff > 0 else "FAIL"
    our_str = f"{our_psnr:.1f}" if our_psnr > 0 else "FAIL"
    ff_str = f"{ff_psnr:.1f}" if ff_psnr > 0 else "FAIL"

    status = "OK" if vs_ff >= 40 else "DIFF" if vs_ff > 0 else "FAIL"
    print(f"{name:<35} {ff_str:>8} {our_str:>8} {vs_str:>10} o={px0_ours:>3} f={px0_ff:>3} {status}")

print(f"\nExact match (>90dB vs ffmpeg): {exact_match}/{total}")
