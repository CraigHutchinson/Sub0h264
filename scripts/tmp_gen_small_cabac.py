#!/usr/bin/env python3
"""Generate small CABAC test fixtures at various MB counts.

Creates minimal frames for isolating per-MB CABAC decode issues:
- 16x16 (1 MB): single MB, no neighbor context
- 32x16 (2 MBs): left neighbor only
- 16x32 (2 MBs): top neighbor only
- 32x32 (4 MBs): both neighbors
- 64x64 (16 MBs): 4x4 MB grid, tests all neighbor patterns

Each with flat gray (Y=121), gradient, and noisy content variants.
"""
import os
import subprocess
import struct
import numpy as np

FIXTURES = "tests/fixtures"
QP = 17  # Same as cabac_flat_main

def gen_yuv(path, w, h, content="flat"):
    """Generate a single-frame raw YUV 4:2:0."""
    if content == "flat":
        y = np.full((h, w), 121, dtype=np.uint8)
        u = np.full((h//2, w//2), 128, dtype=np.uint8)
        v = np.full((h//2, w//2), 128, dtype=np.uint8)
    elif content == "gradient":
        y = np.fromfunction(lambda r, c: 50 + (c * 150 // w) + (r * 50 // h),
                           (h, w), dtype=int).clip(0, 255).astype(np.uint8)
        u = np.full((h//2, w//2), 128, dtype=np.uint8)
        v = np.full((h//2, w//2), 128, dtype=np.uint8)
    elif content == "noisy":
        rng = np.random.RandomState(42)
        y = rng.randint(60, 200, (h, w), dtype=np.uint8)
        u = rng.randint(100, 160, (h//2, w//2), dtype=np.uint8)
        v = rng.randint(100, 160, (h//2, w//2), dtype=np.uint8)
    elif content == "checkerboard":
        y = np.fromfunction(
            lambda r, c: np.where(((r // 4) + (c // 4)) % 2 == 0, 80, 180),
            (h, w)).astype(np.uint8)
        u = np.full((h//2, w//2), 128, dtype=np.uint8)
        v = np.full((h//2, w//2), 128, dtype=np.uint8)
    else:
        raise ValueError(f"Unknown content: {content}")

    with open(path, 'wb') as f:
        f.write(y.tobytes())
        f.write(u.tobytes())
        f.write(v.tobytes())
    return path

def encode_cabac(yuv_path, h264_path, w, h):
    """Encode with CABAC (Main profile), single I-frame."""
    cmd = [
        "ffmpeg", "-y",
        "-f", "rawvideo", "-pix_fmt", "yuv420p",
        "-s", f"{w}x{h}", "-r", "25",
        "-i", yuv_path,
        "-frames:v", "1",
        "-c:v", "libx264",
        "-profile:v", "main",
        "-preset", "medium",
        "-qp", str(QP),
        "-g", "1", "-bf", "0",
        "-x264-params", "cabac=1:no-8x8dct=1:bframes=0:keyint=1:min-keyint=1",
        h264_path
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  FAILED: {result.stderr[-200:]}")
        return False
    return True

# Generate fixtures
sizes = [
    ("1mb",  16, 16,  "1 MB - no neighbors"),
    ("2mb_h", 32, 16, "2 MBs horizontal - left neighbor only"),
    ("2mb_v", 16, 32, "2 MBs vertical - top neighbor only"),
    ("4mb",  32, 32,  "4 MBs (2x2) - both neighbors"),
    ("16mb", 64, 64,  "16 MBs (4x4) - full neighbor grid"),
]

contents = ["flat", "gradient", "noisy", "checkerboard"]

generated = []
for size_name, w, h, desc in sizes:
    for content in contents:
        name = f"cabac_{size_name}_{content}"
        yuv_path = os.path.join(FIXTURES, f"{name}_raw.yuv")
        h264_path = os.path.join(FIXTURES, f"{name}.h264")

        if os.path.exists(h264_path):
            print(f"SKIP {name}: already exists")
            continue

        print(f"Generating {name} ({w}x{h}, {desc})...")
        gen_yuv(yuv_path, w, h, content)
        if encode_cabac(yuv_path, h264_path, w, h):
            h264_size = os.path.getsize(h264_path)
            yuv_size = os.path.getsize(yuv_path)
            print(f"  {h264_path}: {h264_size:,} bytes, raw: {yuv_size:,} bytes")
            generated.append(name)
        else:
            # Clean up failed
            if os.path.exists(yuv_path): os.remove(yuv_path)

print(f"\nGenerated {len(generated)} fixtures")
for name in generated:
    print(f"  {name}")
