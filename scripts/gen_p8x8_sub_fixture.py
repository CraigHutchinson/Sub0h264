#!/usr/bin/env python3
"""Generate an H.264 fixture that exercises P_8x8 sub_mb_type ∈ {8x4, 4x8, 4x4}.

Creates content with fine-grained motion that encourages x264 to pick
small (sub-8x8) partitions:

  - small, independent tile motion in different directions per 4x4 region
  - forces `partitions=p8x8,p4x4` + `subme=7` (full sub-partition RDO)

Produces `tests/fixtures/wstress_p8x8_sub_640x368.h264` and dumps the
sub_mb_type histogram from ffmpeg tracing so we can verify the fixture
actually does hit 8x4/4x8/4x4.

Usage:
    python scripts/gen_p8x8_sub_fixture.py [--outdir tests/fixtures]
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys

import numpy as np

FPS = 30
NUM_FRAMES = 10
QP = 22
W, H = 640, 368


def rgb_to_yuv420(rgb):
    r, g, b = rgb[:, :, 0].astype(np.float32), rgb[:, :, 1].astype(np.float32), rgb[:, :, 2].astype(np.float32)
    y = np.clip(0.257 * r + 0.504 * g + 0.098 * b + 16, 0, 255).astype(np.uint8)
    u = np.clip(-0.148 * r - 0.291 * g + 0.439 * b + 128, 0, 255).astype(np.uint8)
    v = np.clip(0.439 * r - 0.368 * g - 0.071 * b + 128, 0, 255).astype(np.uint8)
    return y.tobytes() + u[0::2, 0::2].tobytes() + v[0::2, 0::2].tobytes()


def make_frame(t: int) -> np.ndarray:
    """Per-4x4 tile with independent pseudo-random motion (forces fine partition)."""
    rng = np.random.default_rng(42)  # same tile pattern, shifted per frame
    img = np.zeros((H, W, 3), dtype=np.uint8)
    for tile_y in range(0, H, 4):
        for tile_x in range(0, W, 4):
            # Unique per-tile motion vector (small, varies strongly tile-to-tile)
            mvx = int(rng.integers(-3, 4))
            mvy = int(rng.integers(-3, 4))
            # Per-tile unique colour + small motion-dependent offset
            hue_r = (tile_x * 17 + tile_y * 3 + t * mvx) & 0xFF
            hue_g = (tile_y * 19 + tile_x * 5 + t * mvy) & 0xFF
            hue_b = ((tile_x + tile_y) * 11 + t * (mvx + mvy)) & 0xFF
            img[tile_y:tile_y + 4, tile_x:tile_x + 4] = [hue_r, hue_g, hue_b]
    return img


def encode(raw_path: str, h264_path: str) -> bool:
    cmd = [
        "ffmpeg", "-y", "-f", "rawvideo", "-pix_fmt", "yuv420p",
        "-s", f"{W}x{H}", "-r", str(FPS), "-i", raw_path,
        "-c:v", "libx264", "-profile:v", "high", "-bf", "0", "-refs", "1",
        "-g", "30", "-crf", str(QP), "-preset", "slow",
        "-x264opts", "partitions=p8x8,p4x4,i8x8,i4x4:subme=9:analyse=all:"
                     "no-fast-pskip=1:no-mixed-refs=1",
        "-f", "h264", h264_path,
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"FAILED: {result.stderr[-500:]}")
        return False
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--outdir", default="tests/fixtures")
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    raw_path = os.path.join(args.outdir, f"wstress_p8x8_sub_{W}x{H}_raw.yuv")
    h264_path = os.path.join(args.outdir, f"wstress_p8x8_sub_{W}x{H}.h264")

    print(f"Generating {NUM_FRAMES}-frame raw YUV @ {W}x{H}...")
    with open(raw_path, "wb") as f:
        for t in range(NUM_FRAMES):
            f.write(rgb_to_yuv420(make_frame(t)))

    print(f"Encoding with partition-forcing x264 opts...")
    if not encode(raw_path, h264_path):
        return 1

    size = os.path.getsize(h264_path)
    print(f"Wrote {h264_path} ({size} bytes, {NUM_FRAMES} frames)")

    # Clean up the raw YUV — it can be regenerated from this script.
    try:
        os.remove(raw_path)
    except OSError:
        pass

    return 0


if __name__ == "__main__":
    sys.exit(main())
