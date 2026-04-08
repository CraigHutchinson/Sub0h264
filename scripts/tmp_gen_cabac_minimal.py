#!/usr/bin/env python3
"""Generate absolutely minimal CABAC fixtures for engine state debugging.

The goal: create the simplest possible bitstreams where we can predict
exactly what every CABAC bin should be, then compare against our decoder.

Fixtures:
1. 1MB all-128 (flat gray) — I_16x16 DC, zero residual, minimal bins
2. 1MB all-0 (black) — I_16x16 DC, zero residual
3. 1MB all-255 (white) — I_16x16 DC, zero residual
4. 1MB uniform non-128 — forces non-zero DC residual
5. 1MB with single non-zero pixel — minimal non-zero residual
6. 1MB horizontal stripes — forces I_4x4 with specific pred modes
7. Explicit I_16x16 encode — force I_16x16 mb_type via x264 params
"""
import os, subprocess, numpy as np

FIXTURES = "tests/fixtures"

def gen_and_encode(name, w, h, y_val_or_array, qp=17, extra_params=""):
    yuv = os.path.join(FIXTURES, f"cabac_min_{name}_raw.yuv")
    h264 = os.path.join(FIXTURES, f"cabac_min_{name}.h264")
    if os.path.exists(h264):
        return

    if isinstance(y_val_or_array, int):
        y = np.full((h, w), y_val_or_array, dtype=np.uint8)
    else:
        y = y_val_or_array.astype(np.uint8)
    u = np.full((h//2, w//2), 128, dtype=np.uint8)
    v = np.full((h//2, w//2), 128, dtype=np.uint8)

    with open(yuv, 'wb') as f:
        f.write(y.tobytes())
        f.write(u.tobytes())
        f.write(v.tobytes())

    params = f"cabac=1:no-8x8dct=1:bframes=0:keyint=1:min-keyint=1{extra_params}"
    cmd = ["ffmpeg", "-y", "-f", "rawvideo", "-pix_fmt", "yuv420p",
           "-s", f"{w}x{h}", "-r", "25", "-i", yuv,
           "-frames:v", "1", "-c:v", "libx264", "-profile:v", "main",
           "-preset", "medium", "-qp", str(qp), "-g", "1", "-bf", "0",
           "-x264-params", params, h264]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode == 0:
        sz = os.path.getsize(h264)
        # Check what ffmpeg decodes
        r2 = subprocess.run(["ffmpeg", "-i", h264, "-vframes", "1", "-pix_fmt", "yuv420p",
                            "-f", "rawvideo", "pipe:"],
                           capture_output=True, timeout=10)
        px0 = r2.stdout[0] if r2.stdout else -1
        print(f"  {name}: {sz:,} bytes, ffmpeg pixel[0]={px0}")
    else:
        print(f"  FAIL {name}: {r.stderr[-100:]}")

# ── Simplest possible: uniform values ────────────────────────────────────
print("=== Uniform value 1MB CABAC (should be I_16x16, minimal bitstream) ===")
for val in [0, 64, 100, 121, 128, 200, 255]:
    gen_and_encode(f"u{val}", 16, 16, val, qp=17)

# ── Force I_16x16 with x264 param ───────────────────────────────────────
print("\n=== Force I_16x16 via x264 ===")
gen_and_encode("i16x16_128", 16, 16, 128, qp=17, extra_params=":i4x4-only=0")

# ── Very low QP (should be near pixel-perfect) ──────────────────────────
print("\n=== Low QP (minimal quantization error) ===")
gen_and_encode("u128_qp1", 16, 16, 128, qp=1)
gen_and_encode("u121_qp1", 16, 16, 121, qp=1)
gen_and_encode("noisy_qp1", 16, 16,
               np.random.RandomState(42).randint(60, 200, (16, 16)), qp=1)

# ── Minimal I_4x4: uniform but non-128 forces small DC residual ─────────
print("\n=== Uniform non-128 at various QP (tests DC residual path) ===")
for qp in [1, 10, 17, 30]:
    gen_and_encode(f"u100_qp{qp}", 16, 16, 100, qp=qp)

# ── Chroma-only content: Y=128, non-neutral chroma ──────────────────────
print("\n=== Chroma-only: Y=128, colored chroma ===")
y = np.full((16, 16), 128, dtype=np.uint8)
yuv_path = os.path.join(FIXTURES, "cabac_min_chroma_raw.yuv")
h264_path = os.path.join(FIXTURES, "cabac_min_chroma.h264")
if not os.path.exists(h264_path):
    u = np.full((8, 8), 80, dtype=np.uint8)  # Strong blue
    v = np.full((8, 8), 200, dtype=np.uint8)  # Strong red
    with open(yuv_path, 'wb') as f:
        f.write(y.tobytes())
        f.write(u.tobytes())
        f.write(v.tobytes())
    cmd = ["ffmpeg", "-y", "-f", "rawvideo", "-pix_fmt", "yuv420p",
           "-s", "16x16", "-r", "25", "-i", yuv_path,
           "-frames:v", "1", "-c:v", "libx264", "-profile:v", "main",
           "-preset", "medium", "-qp", "17", "-g", "1", "-bf", "0",
           "-x264-params", "cabac=1:no-8x8dct=1:bframes=0:keyint=1:min-keyint=1",
           h264_path]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode == 0:
        print(f"  chroma: {os.path.getsize(h264_path):,} bytes")

print("\nDone.")
