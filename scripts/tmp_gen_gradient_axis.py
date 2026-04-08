#!/usr/bin/env python3
"""Generate gradient fixtures along 8 major axes to isolate CAVLC rounding issue.

Axes: horizontal (→), vertical (↓), diagonal (↘↗↙↖),
      and steep/shallow variants. Both CABAC and CAVLC versions.

The gradient fills the ENTIRE 16x16 MB with a smooth ramp from low to high.
This forces the encoder to use specific prediction modes and creates dense
non-zero AC coefficients that stress the IDCT rounding path.
"""
import os
import subprocess
import shutil
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

def encode(yuv, h264, w, h, profile="main"):
    cabac = "1" if profile == "main" else "0"
    cmd = ["ffmpeg", "-y", "-f", "rawvideo", "-pix_fmt", "yuv420p",
           "-s", f"{w}x{h}", "-r", "25", "-i", yuv,
           "-frames:v", "1", "-c:v", "libx264", "-profile:v", profile,
           "-preset", "medium", "-qp", str(QP), "-g", "1", "-bf", "0",
           "-x264-params", f"cabac={cabac}:no-8x8dct=1:bframes=0:keyint=1:min-keyint=1",
           h264]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode == 0

def make(name, w, h, y_plane):
    """Generate both CABAC and CAVLC versions."""
    for prefix, profile in [("cabac", "main"), ("cavlc", "baseline")]:
        fullname = f"{prefix}_{name}"
        yuv = os.path.join(FIXTURES, f"{fullname}_raw.yuv")
        h264 = os.path.join(FIXTURES, f"{fullname}.h264")
        if os.path.exists(h264):
            continue
        gen_yuv(yuv, w, h, y_plane)
        if encode(yuv, h264, w, h, profile):
            print(f"  {fullname}: {os.path.getsize(h264):,} bytes")
        else:
            print(f"  FAIL {fullname}")
            if os.path.exists(yuv): os.remove(yuv)

W, H = 16, 16
low, high = 50, 210  # Wide range to create strong gradients

# ── 8 gradient axes ──────────────────────────────────────────────────────

print("=== Gradient axis fixtures (16x16, 1 MB) ===")

# 1. Horizontal: left→right
y = np.fromfunction(lambda r, c: low + (high-low) * c / (W-1), (H, W)).astype(np.uint8)
make("1mb_grad_h", W, H, y)

# 2. Vertical: top→bottom
y = np.fromfunction(lambda r, c: low + (high-low) * r / (H-1), (H, W)).astype(np.uint8)
make("1mb_grad_v", W, H, y)

# 3. Diagonal ↘ (top-left → bottom-right)
y = np.fromfunction(lambda r, c: low + (high-low) * (r+c) / (H+W-2), (H, W)).astype(np.uint8)
make("1mb_grad_diag_se", W, H, y)

# 4. Diagonal ↗ (bottom-left → top-right)
y = np.fromfunction(lambda r, c: low + (high-low) * ((H-1-r)+c) / (H+W-2), (H, W)).astype(np.uint8)
make("1mb_grad_diag_ne", W, H, y)

# 5. Reverse horizontal: right→left
y = np.fromfunction(lambda r, c: high - (high-low) * c / (W-1), (H, W)).astype(np.uint8)
make("1mb_grad_h_rev", W, H, y)

# 6. Reverse vertical: bottom→top
y = np.fromfunction(lambda r, c: high - (high-low) * r / (H-1), (H, W)).astype(np.uint8)
make("1mb_grad_v_rev", W, H, y)

# 7. Diagonal ↙ (top-right → bottom-left)
y = np.fromfunction(lambda r, c: low + (high-low) * (r+(W-1-c)) / (H+W-2), (H, W)).astype(np.uint8)
make("1mb_grad_diag_sw", W, H, y)

# 8. Diagonal ↖ (bottom-right → top-left)
y = np.fromfunction(lambda r, c: high - (high-low) * (r+c) / (H+W-2), (H, W)).astype(np.uint8)
make("1mb_grad_diag_nw", W, H, y)

# ── Steepness variants on horizontal ─────────────────────────────────────

print("\n=== Gradient steepness variants (16x16) ===")

# Shallow: only 20 levels of range
y = np.fromfunction(lambda r, c: 118 + 20 * c / (W-1), (H, W)).astype(np.uint8)
make("1mb_grad_h_shallow", W, H, y)

# Steep: full 0-255 range
y = np.fromfunction(lambda r, c: 255 * c / (W-1), (H, W)).astype(np.uint8)
make("1mb_grad_h_steep", W, H, y)

# ── 4MB (32x32) gradients for neighbor influence ─────────────────────────

print("\n=== 4MB gradients (32x32) ===")
W4, H4 = 32, 32

y = np.fromfunction(lambda r, c: low + (high-low) * c / (W4-1), (H4, W4)).astype(np.uint8)
make("4mb_grad_h", W4, H4, y)

y = np.fromfunction(lambda r, c: low + (high-low) * r / (H4-1), (H4, W4)).astype(np.uint8)
make("4mb_grad_v", W4, H4, y)

y = np.fromfunction(lambda r, c: low + (high-low) * (r+c) / (H4+W4-2), (H4, W4)).astype(np.uint8)
make("4mb_grad_diag", W4, H4, y)

print("\nDone.")
