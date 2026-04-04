#!/usr/bin/env python3
"""Decode an H.264 fixture with our decoder (via test binary) and compare
against ffmpeg's decode, measuring PSNR.

This works by adding the fixture to the test_synthetic_quality test paths
and running the test binary with a specific test case pattern.

For quick PSNR checks, this script just decodes with ffmpeg and compares
against the raw source (ground truth).

Usage:
    python scripts/decode_and_compare.py <h264> <raw_yuv> --width W --height H

SPDX-License-Identifier: MIT
"""
import argparse
import os
import subprocess
import sys

import numpy as np


def psnr(a, b):
    diff = a.astype(np.float64) - b.astype(np.float64)
    mse = np.mean(diff ** 2)
    if mse == 0:
        return 999.0
    return 10 * np.log10(255.0 ** 2 / mse)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("h264", help="H.264 file to decode")
    parser.add_argument("raw", help="Raw YUV ground truth")
    parser.add_argument("--width", "-W", type=int, default=320)
    parser.add_argument("--height", "-H", type=int, default=240)
    parser.add_argument("--frames", type=int, default=1)
    args = parser.parse_args()

    w, h = args.width, args.height
    frame_size = w * h + 2 * (w // 2) * (h // 2)

    # Decode with ffmpeg
    ffmpeg_out = "build/decode_compare_ffmpeg.yuv"
    subprocess.run(["ffmpeg", "-y", "-i", args.h264, f"-vframes", str(args.frames),
                    "-pix_fmt", "yuv420p", ffmpeg_out],
                   capture_output=True, text=True)

    if not os.path.exists(ffmpeg_out):
        print("ffmpeg decode failed", file=sys.stderr)
        sys.exit(1)

    raw = np.frombuffer(open(args.raw, "rb").read(frame_size * args.frames), dtype=np.uint8)
    ff = np.frombuffer(open(ffmpeg_out, "rb").read(frame_size * args.frames), dtype=np.uint8)

    for f in range(args.frames):
        off = f * frame_size
        raw_y = raw[off:off + w * h].reshape(h, w)
        ff_y = ff[off:off + w * h].reshape(h, w)
        p = psnr(raw_y, ff_y)
        print(f"Frame {f}: ffmpeg vs raw PSNR = {p:.1f} dB, "
              f"Y mean: ffmpeg={ff_y.mean():.1f} raw={raw_y.mean():.1f}")


if __name__ == "__main__":
    main()
