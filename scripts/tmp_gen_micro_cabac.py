#!/usr/bin/env python3
"""Generate micro CABAC test fixtures targeting specific decode paths.

These isolate individual spec sections:
- DC-only: forces I_16x16 with minimal residual (tests Hadamard + DC dequant)
- Single-coeff: one non-zero AC per block (tests sig/last/level for 1 coeff)
- All-zero CBP: forces CBP=0 (tests CBP decode + zero-residual path)
- Max-coeff: dense coefficients (tests full sig/last map + multi-level)
- Horizontal-pred: strong horizontal edges (forces horizontal pred mode)
- Vertical-pred: strong vertical edges (forces vertical pred mode)
- QP sweep: same content at QP=10,20,30,40 (tests dequant at different scales)
"""
import os
import subprocess
import numpy as np

FIXTURES = "tests/fixtures"

def gen_yuv(path, w, h, y_plane, u_val=128, v_val=128):
    """Write a YUV 4:2:0 frame."""
    u = np.full((h//2, w//2), u_val, dtype=np.uint8)
    v = np.full((h//2, w//2), v_val, dtype=np.uint8)
    with open(path, 'wb') as f:
        f.write(y_plane.astype(np.uint8).tobytes())
        f.write(u.tobytes())
        f.write(v.tobytes())

def encode(yuv, h264, w, h, qp=17, extra_params=""):
    params = f"cabac=1:no-8x8dct=1:bframes=0:keyint=1:min-keyint=1{extra_params}"
    cmd = ["ffmpeg", "-y", "-f", "rawvideo", "-pix_fmt", "yuv420p",
           "-s", f"{w}x{h}", "-r", "25", "-i", yuv,
           "-frames:v", "1", "-c:v", "libx264", "-profile:v", "main",
           "-preset", "medium", "-qp", str(qp), "-g", "1", "-bf", "0",
           "-x264-params", params, h264]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode == 0

generated = []

def make(name, w, h, y_plane, qp=17, u_val=128, v_val=128, extra=""):
    yuv = os.path.join(FIXTURES, f"{name}_raw.yuv")
    h264 = os.path.join(FIXTURES, f"{name}.h264")
    if os.path.exists(h264):
        print(f"  SKIP {name}")
        return
    gen_yuv(yuv, w, h, y_plane, u_val, v_val)
    if encode(yuv, h264, w, h, qp, extra):
        sz = os.path.getsize(h264)
        print(f"  {name}: {sz:,} bytes")
        generated.append(name)
    else:
        print(f"  FAIL {name}")
        os.remove(yuv)

# ── Constant value sweep (forces I_16x16 DC) ────────────────────────────
print("=== Constant value 1MB (forces I_16x16 DC path) ===")
for val in [0, 64, 121, 128, 200, 255]:
    y = np.full((16, 16), val, dtype=np.uint8)
    make(f"cabac_1mb_const{val}", 16, 16, y)

# ── Horizontal stripes (forces horizontal prediction mode) ───────────────
print("\n=== Horizontal stripes 1MB ===")
y = np.zeros((16, 16), dtype=np.uint8)
for r in range(16):
    y[r, :] = 50 + r * 12  # 50 to 230
make("cabac_1mb_hstripes", 16, 16, y)

# ── Vertical stripes (forces vertical prediction mode) ───────────────────
print("\n=== Vertical stripes 1MB ===")
y = np.zeros((16, 16), dtype=np.uint8)
for c in range(16):
    y[:, c] = 50 + c * 12
make("cabac_1mb_vstripes", 16, 16, y)

# ── Diagonal gradient (tests diagonal prediction modes) ──────────────────
print("\n=== Diagonal gradient 1MB ===")
y = np.fromfunction(lambda r, c: 50 + (r + c) * 5, (16, 16)).clip(0,255).astype(np.uint8)
make("cabac_1mb_diag", 16, 16, y)

# ── Half black / half white (strong edge, tests intra mode selection) ────
print("\n=== Split 1MB (left=64, right=192) ===")
y = np.zeros((16, 16), dtype=np.uint8)
y[:, :8] = 64
y[:, 8:] = 192
make("cabac_1mb_split_v", 16, 16, y)

y = np.zeros((16, 16), dtype=np.uint8)
y[:8, :] = 64
y[8:, :] = 192
make("cabac_1mb_split_h", 16, 16, y)

# ── QP sweep on same content ─────────────────────────────────────────────
print("\n=== QP sweep on noisy 1MB ===")
rng = np.random.RandomState(42)
noisy = rng.randint(60, 200, (16, 16)).astype(np.uint8)
for qp in [10, 17, 24, 30, 40]:
    make(f"cabac_1mb_noisy_qp{qp}", 16, 16, noisy, qp=qp)

# ── Chroma-heavy content (non-neutral chroma) ───────────────────────────
print("\n=== Chroma content 1MB ===")
y = np.full((16, 16), 128, dtype=np.uint8)
make("cabac_1mb_red", 16, 16, y, u_val=90, v_val=200)   # reddish
make("cabac_1mb_blue", 16, 16, y, u_val=200, v_val=90)   # bluish

# ── 2MB with distinct halves (tests neighbor influence) ──────────────────
print("\n=== 2MB distinct halves (32x16) ===")
y = np.zeros((16, 32), dtype=np.uint8)
y[:, :16] = 80   # left MB = dark
y[:, 16:] = 200  # right MB = bright
make("cabac_2mb_contrast", 32, 16, y)

# ── 4MB with quadrants ──────────────────────────────────────────────────
print("\n=== 4MB quadrants (32x32) ===")
y = np.zeros((32, 32), dtype=np.uint8)
y[:16, :16] = 60    # TL dark
y[:16, 16:] = 140   # TR mid
y[16:, :16] = 200   # BL bright
y[16:, 16:] = 100   # BR mid-dark
make("cabac_4mb_quadrants", 32, 32, y)

print(f"\nGenerated {len(generated)} new fixtures")
