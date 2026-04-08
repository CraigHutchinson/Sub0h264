#!/usr/bin/env python3
"""Generate fixtures to isolate the horizontal IDCT rounding accumulation.

Strategy: create content that forces specific decode conditions:

1. FLAT with single-pixel perturbation — forces non-zero DC coeff
   but minimal AC. If this still has error, the issue is in DC dequant.

2. Single horizontal row of pixels different — forces horizontal AC
   but minimal vertical AC. Isolates horizontal IDCT pass.

3. Single vertical column different — forces vertical AC but minimal
   horizontal AC. Isolates vertical IDCT pass.

4. 4x4 block with known exact coefficient — hand-craft a residual
   that produces exactly one non-zero coefficient at each position.

5. Content that forces I_16x16 vs I_4x4 — compare I_16x16 (Hadamard
   + DC dequant path) vs I_4x4 (direct 4x4 DCT path).

6. Wide frame (64x16 = 4 MBs in a row, 1 row) — isolates horizontal
   MB-to-MB prediction chain without vertical.

7. Tall frame (16x64 = 4 MBs in a column) — isolates vertical
   MB-to-MB chain without horizontal.
"""
import os
import subprocess
import numpy as np

FIXTURES = "tests/fixtures"
QP = 17

def gen_yuv(path, w, h, y_plane, u_val=128, v_val=128):
    u = np.full((h//2, w//2), u_val, dtype=np.uint8)
    v = np.full((h//2, w//2), v_val, dtype=np.uint8)
    with open(path, 'wb') as f:
        f.write(y_plane.astype(np.uint8).tobytes())
        f.write(u.tobytes())
        f.write(v.tobytes())

def encode_both(name, w, h, y_plane, qp=QP):
    for prefix, profile, cabac in [("cavlc", "baseline", "0"), ("cabac", "main", "1")]:
        fullname = f"{prefix}_{name}"
        yuv = os.path.join(FIXTURES, f"{fullname}_raw.yuv")
        h264 = os.path.join(FIXTURES, f"{fullname}.h264")
        if os.path.exists(h264): continue
        gen_yuv(yuv, w, h, y_plane)
        cmd = ["ffmpeg", "-y", "-f", "rawvideo", "-pix_fmt", "yuv420p",
               "-s", f"{w}x{h}", "-r", "25", "-i", yuv,
               "-frames:v", "1", "-c:v", "libx264", "-profile:v", profile,
               "-preset", "medium", "-qp", str(qp), "-g", "1", "-bf", "0",
               "-x264-params", f"cabac={cabac}:no-8x8dct=1:bframes=0:keyint=1:min-keyint=1",
               h264]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode == 0:
            print(f"  {fullname}: {os.path.getsize(h264):,} bytes")
        else:
            print(f"  FAIL {fullname}")

# ══════════════════════════════════════════════════════════════════════════
# Test 1: Flat with single-pixel perturbation
# If DC-only blocks still accumulate error, the issue is in DC dequant/IDCT.
# ══════════════════════════════════════════════════════════════════════════
print("=== Test 1: Flat + single pixel perturbation ===")
# 128 everywhere except pixel (7,7) = 200. Forces a non-zero residual
# but the prediction should be flat 128 for most blocks.
y = np.full((16, 16), 128, dtype=np.uint8)
y[7, 7] = 200
encode_both("1mb_flat_perturb", 16, 16, y)

# ══════════════════════════════════════════════════════════════════════════
# Test 2: Horizontal-only AC content
# A single bright row forces horizontal AC coefficients only.
# ══════════════════════════════════════════════════════════════════════════
print("\n=== Test 2: Single bright row (horizontal AC) ===")
y = np.full((16, 16), 100, dtype=np.uint8)
y[8, :] = 200  # One bright row in the middle
encode_both("1mb_hrow", 16, 16, y)

# ══════════════════════════════════════════════════════════════════════════
# Test 3: Vertical-only AC content
# A single bright column forces vertical AC coefficients only.
# ══════════════════════════════════════════════════════════════════════════
print("\n=== Test 3: Single bright column (vertical AC) ===")
y = np.full((16, 16), 100, dtype=np.uint8)
y[:, 8] = 200  # One bright column
encode_both("1mb_vcol", 16, 16, y)

# ══════════════════════════════════════════════════════════════════════════
# Test 4: Wide (64x16) — 4 MBs horizontal chain only
# Gradient across 4 MBs in a row. No vertical MB neighbors at all.
# This isolates the horizontal MB-to-MB prediction chain.
# ══════════════════════════════════════════════════════════════════════════
print("\n=== Test 4: Wide frame 64x16 (horizontal MB chain only) ===")
y = np.fromfunction(lambda r, c: 50 + 160 * c / 63, (16, 64)).clip(0,255).astype(np.uint8)
encode_both("4mb_wide_grad_h", 64, 16, y)

# Same but flat — should be pixel-perfect if the chain itself is OK
y = np.full((16, 64), 121, dtype=np.uint8)
encode_both("4mb_wide_flat", 64, 16, y)

# ══════════════════════════════════════════════════════════════════════════
# Test 5: Tall (16x64) — 4 MBs vertical chain only
# ══════════════════════════════════════════════════════════════════════════
print("\n=== Test 5: Tall frame 16x64 (vertical MB chain only) ===")
y = np.fromfunction(lambda r, c: 50 + 160 * r / 63, (64, 16)).clip(0,255).astype(np.uint8)
encode_both("4mb_tall_grad_v", 16, 64, y)

y = np.full((64, 16), 121, dtype=np.uint8)
encode_both("4mb_tall_flat", 16, 64, y)

# ══════════════════════════════════════════════════════════════════════════
# Test 6: Forced prediction modes via content shape
# Content with strong horizontal edges → encoder picks horizontal mode
# Content with strong vertical edges → encoder picks vertical mode
# Content with uniform blocks → encoder picks DC mode
# ══════════════════════════════════════════════════════════════════════════
print("\n=== Test 6: Forced prediction modes (4MB 32x32) ===")

# All blocks same value → DC prediction, zero residual
y = np.full((32, 32), 121, dtype=np.uint8)
encode_both("4mb_dc_only", 32, 32, y)

# Strong horizontal bands within each 4x4 block
# Each row of each 4x4 block is constant but different
y = np.zeros((32, 32), dtype=np.uint8)
for r in range(32):
    y[r, :] = 80 + (r % 4) * 40  # 80, 120, 160, 200 repeating
encode_both("4mb_hbands", 32, 32, y)

# Strong vertical bands within each 4x4 block
y = np.zeros((32, 32), dtype=np.uint8)
for c in range(32):
    y[:, c] = 80 + (c % 4) * 40
encode_both("4mb_vbands", 32, 32, y)

# ══════════════════════════════════════════════════════════════════════════
# Test 7: Single 4x4 block with exact known residual
# Flat except one 4x4 block has a specific pattern.
# The surrounding blocks have zero residual, so any error in the
# non-flat block doesn't accumulate through prediction chain.
# ══════════════════════════════════════════════════════════════════════════
print("\n=== Test 7: Isolated 4x4 block (no prediction chain) ===")
y = np.full((16, 16), 128, dtype=np.uint8)
# Top-left 4x4 block has a gradient
for r in range(4):
    for c in range(4):
        y[r, c] = 80 + r * 20 + c * 10
encode_both("1mb_isolated_block", 16, 16, y)

# ══════════════════════════════════════════════════════════════════════════
# Test 8: Low QP (fine quantization) vs High QP
# At very low QP, the residual should be more accurate.
# If the rounding issue is in dequant, it scales with QP.
# If it's in IDCT, it's constant regardless of QP.
# ══════════════════════════════════════════════════════════════════════════
print("\n=== Test 8: QP sweep on horizontal gradient ===")
y = np.fromfunction(lambda r, c: 50 + 160 * c / 15, (16, 16)).clip(0,255).astype(np.uint8)
for qp in [5, 10, 17, 24, 36, 48]:
    encode_both(f"1mb_grad_h_qp{qp}", 16, 16, y, qp=qp)

print("\nDone.")
