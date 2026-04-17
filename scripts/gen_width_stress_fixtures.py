#!/usr/bin/env python3
"""Generate width-stress synthetic fixtures for decoder coverage.

Covers the gap between 320x240 (20 MBs wide) and 640x480 (40 MBs wide).
Isolates per-intra-mode and cropping issues that manifest at wide frames.

Motivating bug: Tapo C110 stream (640x368 High profile) decodes at
6.43 dB PSNR — IDR diverges at MB(15,0). No existing synthetic fixture
exercises resolutions wider than 320x240 with CABAC and controlled
intra modes.

These fixtures also permanently cover the regression path.

Usage:
    python scripts/gen_width_stress_fixtures.py [--outdir tests/fixtures]

Requires: ffmpeg with libx264, numpy.

SPDX-License-Identifier: MIT
"""
import argparse
import os
import subprocess

import numpy as np

FPS = 30
NUM_FRAMES = 5  # small — these are coverage fixtures, not perf fixtures
QP = 20


def rgb_to_yuv420(rgb):
    """Convert RGB frame to I420 YUV bytes."""
    r = rgb[:, :, 0].astype(np.float32)
    g = rgb[:, :, 1].astype(np.float32)
    b = rgb[:, :, 2].astype(np.float32)
    y = np.clip(0.257 * r + 0.504 * g + 0.098 * b + 16, 0, 255).astype(np.uint8)
    u = np.clip(-0.148 * r - 0.291 * g + 0.439 * b + 128, 0, 255).astype(np.uint8)
    v = np.clip(0.439 * r - 0.368 * g - 0.071 * b + 128, 0, 255).astype(np.uint8)
    return y.tobytes() + u[0::2, 0::2].tobytes() + v[0::2, 0::2].tobytes()


def make_gradient(w, h):
    """Horizontal gradient content — exercises neighbor-sample prediction."""
    img = np.zeros((h, w, 3), dtype=np.uint8)
    for x in range(w):
        v = int(x * 255 / max(1, w - 1))
        img[:, x] = [v, v, v]
    return img


def make_checkerboard_small(w, h, block=8):
    """Fine checkerboard — exercises I_4x4 / I_8x8 mode selection."""
    img = np.zeros((h, w, 3), dtype=np.uint8)
    for y in range(h):
        for x in range(w):
            if ((x // block) + (y // block)) % 2 == 0:
                img[y, x] = [210, 110, 60]
            else:
                img[y, x] = [60, 160, 210]
    return img


def make_complex_left_flat_right(w, h, split_col):
    """Complex content on left, flat grey on right.

    Mimics Tapo IDR encoding pattern: ~15 MBs of complex content followed
    by flat. Exposes the transition boundary at a configurable column.
    """
    img = np.full((h, w, 3), 128, dtype=np.uint8)
    # Complex left: noisy gradient
    rng = np.random.RandomState(42)
    for x in range(min(split_col, w)):
        base = int(x * 180 / max(1, split_col - 1)) + 40
        noise = rng.randint(-30, 31, h)
        for y in range(h):
            v = max(0, min(255, base + noise[y]))
            img[y, x] = [v, v, v]
    return img


def encode(raw_path, h264_path, profile, w, h, nframes, tune=None):
    """Encode raw YUV to H.264."""
    if profile == "baseline":
        flags = ["-profile:v", "baseline", "-bf", "0", "-refs", "1"]
    else:
        flags = ["-profile:v", "high", "-bf", "0", "-refs", "1"]
    x264_opts = []
    if tune == "i16x16_only":
        # Force I_16x16 intra only
        x264_opts = ["-x264opts", "no-8x8dct:analyse=i4x4,i8x8:intra-refresh=0"]
    elif tune == "i8x8_only":
        x264_opts = ["-x264opts", "8x8dct=1:analyse=i8x8"]
    elif tune == "i4x4_only":
        x264_opts = ["-x264opts", "no-8x8dct:analyse=i4x4"]
    cmd = [
        "ffmpeg", "-y", "-f", "rawvideo", "-pix_fmt", "yuv420p",
        "-s", f"{w}x{h}", "-r", str(FPS), "-i", raw_path,
        *flags, "-g", "1", "-crf", str(QP), "-preset", "medium",
        *x264_opts,
        "-f", "h264", h264_path,
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"    FAILED: {result.stderr[-300:]}")
        return False
    return True


def gen_content(name, w, h, outdir):
    """Generate raw YUV content and return path."""
    raw_path = os.path.join(outdir, f"wstress_{name}_{w}x{h}_raw.yuv")

    with open(raw_path, "wb") as f:
        if "gradient" in name:
            frame_rgb = make_gradient(w, h)
        elif "checker" in name:
            frame_rgb = make_checkerboard_small(w, h)
        elif "complex_flat" in name:
            # Split at exactly 15 MBs (column 240) — matches Tapo bug pattern
            frame_rgb = make_complex_left_flat_right(w, h, split_col=240)
        else:
            frame_rgb = make_gradient(w, h)  # default

        frame_data = rgb_to_yuv420(frame_rgb)
        for _ in range(NUM_FRAMES):
            f.write(frame_data)
    return raw_path


# (name, width, height, profile, tune, description)
FIXTURES = [
    # Width-stress: single-row wide frames (1 MB row of 16 pixels)
    ("wide16_gradient", 256, 16, "high", None, "16 MB wide × 1 MB tall — baseline wide-row test"),
    ("wide24_gradient", 384, 16, "high", None, "24 MB wide — between 20 (works) and 40 (fails)"),
    ("wide40_gradient", 640, 16, "high", None, "40 MB wide — matches Tapo width"),

    # 640x368 minimal reproductions matching Tapo dimensions
    ("tapo_size_gradient", 640, 368, "high", None, "640x368 High — Tapo dimensions, gradient"),
    ("tapo_size_complex_flat", 640, 368, "high", None,
     "640x368 High — complex left 15 MBs + flat right (reproduces Tapo bug pattern)"),

    # Intra-mode isolation at Tapo dimensions
    ("tapo_size_i16x16", 640, 368, "high", "i16x16_only", "640x368 High — I_16x16 only"),
    ("tapo_size_i8x8", 640, 368, "high", "i8x8_only", "640x368 High — I_8x8 only (High profile)"),
    ("tapo_size_i4x4", 640, 368, "high", "i4x4_only", "640x368 High — I_4x4 only"),

    # Baseline variants (no CABAC)
    ("tapo_size_baseline", 640, 368, "baseline", None, "640x368 Baseline CAVLC — isolate CABAC vs CAVLC"),
    ("wide40_baseline", 640, 16, "baseline", None, "40 MB wide × 1 MB tall, Baseline CAVLC"),
]


def main():
    parser = argparse.ArgumentParser(description="Generate width-stress fixtures")
    parser.add_argument("--outdir", default="tests/fixtures")
    args = parser.parse_args()
    os.makedirs(args.outdir, exist_ok=True)

    generated = 0
    for name, w, h, profile, tune, desc in FIXTURES:
        print(f"[{name}] {desc}")
        raw_path = gen_content(name, w, h, args.outdir)
        h264_name = f"wstress_{name}_{w}x{h}.h264"
        h264_path = os.path.join(args.outdir, h264_name)
        if encode(raw_path, h264_path, profile, w, h, NUM_FRAMES, tune):
            size = os.path.getsize(h264_path)
            print(f"    OK: {h264_name} ({size} bytes)")
            generated += 1

    print(f"\nGenerated {generated}/{len(FIXTURES)} width-stress fixtures.")


if __name__ == "__main__":
    main()
