#!/usr/bin/env python3
"""Test P-frame micro fixtures: per-frame PSNR for ffmpeg and our decoder."""
import subprocess, os, math

FIXTURES = "tests/fixtures"
TRACE = os.path.join("build", "test_apps", "trace", "Release", "sub0h264_trace.exe")
W, H, NFRAMES = 32, 32, 3

names = ["static","hpan1","hpan4","hpan16","vpan1","diag11","large84","single_mb","brightness"]

def psnr(a, b, size):
    if len(a) < size or len(b) < size: return -1
    mse = sum((a[i]-b[i])**2 for i in range(size)) / size
    return 99.0 if mse < 0.001 else 10*math.log10(255*255/mse)

print(f"{'Fixture':<15} {'F0_ff':>6} {'F1_ff':>6} {'F2_ff':>6}  {'F0_us':>6} {'F1_us':>6} {'F2_us':>6}  {'gap_F1':>7} {'gap_F2':>7}")
print("-" * 82)

for name in names:
    h264 = os.path.join(FIXTURES, f"cavlc_pf_{name}.h264")
    raw = os.path.join(FIXTURES, f"cavlc_pf_{name}_raw.yuv")
    if not os.path.exists(h264): continue

    raw_data = open(raw, "rb").read()
    frame_size = W * H * 3 // 2

    # ffmpeg
    r = subprocess.run(["ffmpeg", "-i", h264, "-pix_fmt", "yuv420p", "-f", "rawvideo", "pipe:"],
                       capture_output=True, timeout=10)
    ff_data = r.stdout

    # Our decoder
    out = f"build/tmp_pf_{name}.yuv"
    subprocess.run([TRACE, h264, "--dump", out], capture_output=True, timeout=10)
    our_data = open(out, "rb").read() if os.path.exists(out) else b""

    ff_p, our_p = [], []
    for fi in range(NFRAMES):
        y_off = fi * frame_size
        raw_y = raw_data[y_off:y_off + W*H]
        ff_y = ff_data[y_off:y_off + W*H] if len(ff_data) >= y_off + W*H else b""
        our_y = our_data[y_off:y_off + W*H] if len(our_data) >= y_off + W*H else b""
        ff_p.append(psnr(raw_y, ff_y, W*H))
        our_p.append(psnr(raw_y, our_y, W*H))

    gap1 = our_p[1] - ff_p[1] if our_p[1] > 0 and ff_p[1] > 0 else float("nan")
    gap2 = our_p[2] - ff_p[2] if our_p[2] > 0 and ff_p[2] > 0 else float("nan")

    ff_s = " ".join(f"{p:6.1f}" for p in ff_p)
    our_s = " ".join(f"{p:6.1f}" for p in our_p)
    g1 = f"{gap1:+7.1f}" if not math.isnan(gap1) else "   N/A"
    g2 = f"{gap2:+7.1f}" if not math.isnan(gap2) else "   N/A"
    print(f"{name:<15} {ff_s}  {our_s}  {g1} {g2}")
