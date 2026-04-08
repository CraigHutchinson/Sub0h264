#!/usr/bin/env python3
"""Test P-frame at increasing sizes to find where quality drops."""
import subprocess, os, math

FIXTURES = "tests/fixtures"
TRACE = os.path.join("build", "test_apps", "trace", "Release", "sub0h264_trace.exe")

sizes = [(32,32), (64,32), (64,64), (128,64), (128,128), (160,128), (320,240)]

def psnr(a, b, size):
    if len(a) < size or len(b) < size: return -1
    mse = sum((a[i]-b[i])**2 for i in range(size)) / size
    return 99.0 if mse < 0.001 else 10*math.log10(255*255/mse)

print(f"{'Size':>10} {'F0_ff':>6} {'F1_ff':>6} {'F2_ff':>6}  {'F0_us':>6} {'F1_us':>6} {'F2_us':>6}  {'gap_F2':>7}")
print("-" * 72)

for w, h in sizes:
    name = f"cavlc_pf_hpan4_{w}x{h}"
    h264 = os.path.join(FIXTURES, f"{name}.h264")
    raw = os.path.join(FIXTURES, f"{name}_raw.yuv")
    if not os.path.exists(h264): continue

    raw_data = open(raw, "rb").read()
    frame_size = w * h * 3 // 2

    r = subprocess.run(["ffmpeg", "-i", h264, "-pix_fmt", "yuv420p", "-f", "rawvideo", "pipe:"],
                       capture_output=True, timeout=30)
    ff_data = r.stdout

    out = f"build/tmp_pf_sz_{w}x{h}.yuv"
    subprocess.run([TRACE, h264, "--dump", out], capture_output=True, timeout=30)
    our_data = open(out, "rb").read() if os.path.exists(out) else b""

    ff_p, our_p = [], []
    for fi in range(3):
        y_off = fi * frame_size
        raw_y = raw_data[y_off:y_off + w*h]
        ff_y = ff_data[y_off:y_off + w*h] if len(ff_data) >= y_off + w*h else b""
        our_y = our_data[y_off:y_off + w*h] if len(our_data) >= y_off + w*h else b""
        ff_p.append(psnr(raw_y, ff_y, w*h))
        our_p.append(psnr(raw_y, our_y, w*h))

    gap = our_p[2] - ff_p[2] if our_p[2] > 0 and ff_p[2] > 0 else float("nan")
    ff_s = " ".join(f"{p:6.1f}" for p in ff_p)
    our_s = " ".join(f"{p:6.1f}" for p in our_p)
    g = f"{gap:+7.1f}" if not math.isnan(gap) else "   N/A"
    print(f"{w:>4}x{h:<4}  {ff_s}  {our_s}  {g}")
