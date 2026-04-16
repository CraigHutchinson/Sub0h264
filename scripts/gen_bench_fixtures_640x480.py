#!/usr/bin/env python3
"""Generate 640x480 benchmark fixtures for Sub0h264 performance testing.

Creates a matrix of content types × 2 profiles (Baseline + High CABAC).
Each fixture has a matching _raw.yuv for PSNR quality gating.

Usage:
    python scripts/gen_bench_fixtures_640x480.py [--outdir tests/fixtures]

Requires: ffmpeg with libx264, numpy.

SPDX-License-Identifier: MIT
"""
import argparse
import os
import subprocess
import sys

import numpy as np

W, H = 640, 480
FPS = 30
NUM_FRAMES = 30  # 30 frames keeps fixture sizes manageable for 16MB ESP32 flash
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


def encode(raw_path, h264_path, profile, w=W, h=H, nframes=NUM_FRAMES):
    """Encode raw YUV to H.264."""
    if profile == "baseline":
        flags = ["-profile:v", "baseline", "-bf", "0", "-refs", "1"]
    else:
        flags = ["-profile:v", "high", "-bf", "0", "-refs", "3"]
    cmd = [
        "ffmpeg", "-y", "-f", "rawvideo", "-pix_fmt", "yuv420p",
        "-s", f"{w}x{h}", "-r", str(FPS), "-i", raw_path,
        *flags, "-g", "10", "-crf", str(QP), "-preset", "ultrafast",
        "-f", "h264", h264_path,
    ]
    subprocess.run(cmd, capture_output=True, check=True)


def make_checkerboard(w, h, block=16):
    """Generate checkerboard texture (double size for scrolling)."""
    img = np.zeros((h * 2, w * 2, 3), dtype=np.uint8)
    for y in range(h * 2):
        for x in range(w * 2):
            if ((x // block) + (y // block)) % 2 == 0:
                img[y, x] = [200, 100, 50]
            else:
                img[y, x] = [50, 150, 200]
    return img


def gen_content(name, outdir):
    """Generate raw YUV content for a given content type."""
    raw_path = os.path.join(outdir, f"bench_{name}_640x480_raw.yuv")
    print(f"  Generating content: {name}...")

    with open(raw_path, "wb") as f:
        if name == "scroll":
            tex = make_checkerboard(W, H, 16)
            for i in range(NUM_FRAMES):
                ox, oy = (i * 4) % W, (i * 2) % H
                f.write(rgb_to_yuv420(tex[oy:oy + H, ox:ox + W]))

        elif name == "scroll_fast":
            tex = make_checkerboard(W, H, 16)
            for i in range(NUM_FRAMES):
                ox, oy = (i * 12) % W, (i * 8) % H
                f.write(rgb_to_yuv420(tex[oy:oy + H, ox:ox + W]))

        elif name == "pan_h":
            tex = make_checkerboard(W, H, 16)
            for i in range(NUM_FRAMES):
                ox = (i * 6) % W
                f.write(rgb_to_yuv420(tex[0:H, ox:ox + W]))

        elif name == "pan_v":
            tex = make_checkerboard(W, H, 16)
            for i in range(NUM_FRAMES):
                oy = (i * 4) % H
                f.write(rgb_to_yuv420(tex[oy:oy + H, 0:W]))

        elif name == "noise_lo":
            rng = np.random.RandomState(42)
            base = np.full((H, W, 3), 128, dtype=np.uint8)
            for _ in range(NUM_FRAMES):
                noise = rng.randint(-30, 31, (H, W, 3), dtype=np.int16)
                frame = np.clip(base.astype(np.int16) + noise, 0, 255).astype(np.uint8)
                f.write(rgb_to_yuv420(frame))

        elif name == "noise_hi":
            rng = np.random.RandomState(99)
            for _ in range(NUM_FRAMES):
                frame = rng.randint(0, 256, (H, W, 3), dtype=np.uint8)
                f.write(rgb_to_yuv420(frame))

        elif name == "ball":
            rng = np.random.RandomState(123)
            bg = np.clip(rng.randint(78, 178, (H, W, 3)), 0, 255).astype(np.uint8)
            bx, by, vx, vy, br = 100.0, 100.0, 7.0, 5.0, 30
            for _ in range(NUM_FRAMES):
                frame = bg.copy()
                cx, cy = int(bx), int(by)
                for y in range(max(0, cy - br), min(H, cy + br)):
                    for x in range(max(0, cx - br), min(W, cx + br)):
                        if (x - cx) ** 2 + (y - cy) ** 2 <= br ** 2:
                            frame[y, x] = [255, 255, 255]
                f.write(rgb_to_yuv420(frame))
                bx += vx; by += vy
                if bx < br or bx >= W - br: vx = -vx
                if by < br or by >= H - br: vy = -vy
                bx = np.clip(bx, br, W - br - 1)
                by = np.clip(by, br, H - br - 1)

        elif name == "still":
            tex = make_checkerboard(W, H, 16)
            frame_data = rgb_to_yuv420(tex[0:H, 0:W])
            for _ in range(NUM_FRAMES):
                f.write(frame_data)

        elif name == "flat":
            black = np.zeros((H, W, 3), dtype=np.uint8)
            frame_data = rgb_to_yuv420(black)
            for _ in range(NUM_FRAMES):
                f.write(frame_data)

    return raw_path


CONTENT_TYPES = [
    "scroll", "scroll_fast", "pan_h", "pan_v",
    "ball", "still", "flat",
]
# noise_lo and noise_hi excluded: unrealistic for camera content and produce
# 13-21 MB bitstreams at QP=20 that exceed the ESP32 16MB flash budget.
# The ball fixture (noisy background) provides entropy stress coverage.


def main():
    parser = argparse.ArgumentParser(description="Generate 640x480 bench fixtures")
    parser.add_argument("--outdir", default="tests/fixtures")
    args = parser.parse_args()
    os.makedirs(args.outdir, exist_ok=True)

    for content in CONTENT_TYPES:
        raw_path = gen_content(content, args.outdir)
        for profile in ["baseline", "high"]:
            h264_name = f"bench_{content}_{profile}_640x480.h264"
            h264_path = os.path.join(args.outdir, h264_name)
            print(f"  Encoding {h264_name}...")
            try:
                encode(raw_path, h264_path, profile)
                size = os.path.getsize(h264_path)
                print(f"    {size} bytes ({size / 1024:.1f} KB)")
            except subprocess.CalledProcessError as e:
                print(f"    FAILED: {e.stderr[-200:] if e.stderr else 'unknown error'}")

    print(f"\nDone. Generated {len(CONTENT_TYPES) * 2} .h264 + {len(CONTENT_TYPES)} _raw.yuv files.")


if __name__ == "__main__":
    main()
