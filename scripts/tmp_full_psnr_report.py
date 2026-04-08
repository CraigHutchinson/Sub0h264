#!/usr/bin/env python3
"""Full PSNR report: our decoder vs ffmpeg for ALL fixtures with raw YUV references."""
import subprocess, os, math, glob

FIXTURES = "tests/fixtures"
TRACE = os.path.join("build", "test_apps", "trace", "Release", "sub0h264_trace.exe")

def get_dims(h264):
    r = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0",
                        "-show_entries", "stream=width,height,nb_frames",
                        "-of", "csv=p=0", h264],
                       capture_output=True, text=True, timeout=5)
    if r.returncode != 0: return None, None, None
    parts = r.stdout.strip().split(",")
    try:
        w, h = int(parts[0]), int(parts[1])
    except (ValueError, IndexError):
        return None, None, None
    nf = None
    if len(parts) > 2:
        try: nf = int(parts[2])
        except ValueError: pass
    return w, h, nf

def psnr_y(a, b, size):
    if len(a) < size or len(b) < size: return -1
    mse = sum((a[i]-b[i])**2 for i in range(size)) / size
    return 99.0 if mse < 0.001 else 10*math.log10(255*255/mse)

def decode_ff(h264, nframes=1):
    cmd = ["ffmpeg", "-i", h264, "-pix_fmt", "yuv420p", "-f", "rawvideo", "pipe:"]
    if nframes: cmd = ["ffmpeg", "-i", h264, "-vframes", str(nframes), "-pix_fmt", "yuv420p", "-f", "rawvideo", "pipe:"]
    r = subprocess.run(cmd, capture_output=True, timeout=30)
    return r.stdout if r.returncode == 0 else b""

def decode_ours(h264):
    out = "build/tmp_psnr_report.yuv"
    r = subprocess.run([TRACE, h264, "--dump", out], capture_output=True, timeout=30)
    if r.returncode != 0 or not os.path.exists(out): return b""
    data = open(out, "rb").read()
    try: os.remove(out)
    except: pass
    return data

# Collect all fixtures with raw references
fixtures = sorted(glob.glob(os.path.join(FIXTURES, "*.h264")))

print("=" * 100)
print(f"{'Fixture':<45} {'Size':>7} {'Type':>5} {'F0_ff':>7} {'F0_us':>7} {'gap':>7} {'Status':>8}")
print("=" * 100)

categories = {}
for h264 in fixtures:
    name = os.path.basename(h264).replace(".h264", "")
    raw = h264.replace(".h264", "_raw.yuv")
    if not os.path.exists(raw): continue

    w, h, nf = get_dims(h264)
    if not w: continue

    frame_y_size = w * h
    frame_size = frame_y_size * 3 // 2

    # Determine type
    if "cabac" in name or "high" in name:
        ftype = "CABAC"
    else:
        ftype = "CAVLC"

    # Decode first frame only for speed
    raw_data = open(raw, "rb").read()
    ff_data = decode_ff(h264, 1)
    our_data = decode_ours(h264)

    ff_p = psnr_y(raw_data[:frame_y_size], ff_data[:frame_y_size], frame_y_size)
    our_p = psnr_y(raw_data[:frame_y_size], our_data[:frame_y_size], frame_y_size) if len(our_data) >= frame_y_size else -1

    gap = our_p - ff_p if our_p > 0 and ff_p > 0 else float("nan")

    if our_p < 0:
        status = "FAIL"
    elif abs(gap) < 1.0:
        status = "OK"
    elif abs(gap) < 5.0:
        status = "CLOSE"
    elif gap < -10:
        status = "BAD"
    else:
        status = "DIFF"

    ff_s = f"{ff_p:7.1f}" if ff_p > 0 else "  FAIL"
    our_s = f"{our_p:7.1f}" if our_p > 0 else "  FAIL"
    gap_s = f"{gap:+7.1f}" if not math.isnan(gap) else "    N/A"

    print(f"{name:<45} {w:>3}x{h:<3} {ftype:>5} {ff_s} {our_s} {gap_s} {status:>8}")

    cat = ftype
    if cat not in categories: categories[cat] = {"ok": 0, "close": 0, "bad": 0, "fail": 0, "total": 0}
    categories[cat]["total"] += 1
    if status == "OK": categories[cat]["ok"] += 1
    elif status == "CLOSE": categories[cat]["close"] += 1
    elif status in ("BAD", "DIFF"): categories[cat]["bad"] += 1
    else: categories[cat]["fail"] += 1

print("=" * 100)
print("\nSummary:")
for cat, counts in sorted(categories.items()):
    print(f"  {cat}: {counts['ok']} OK, {counts['close']} close, {counts['bad']} bad, {counts['fail']} fail / {counts['total']} total")
