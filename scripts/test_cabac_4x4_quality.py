#!/usr/bin/env python3
"""Quick CABAC 4x4-only quality check.

Decodes a CABAC-encoded (Main profile, no 8x8) fixture and compares
against both ffmpeg's decode and the raw ground truth.

Usage:
    python scripts/test_cabac_4x4_quality.py

Requires: ffmpeg, numpy. Uses build/cabac_idr_only.h264 fixture.

SPDX-License-Identifier: MIT
"""
import os
import subprocess
import sys

import numpy as np


def main():
    w, h = 320, 240
    frame_size = w * h + 2 * (w // 2) * (h // 2)

    fixture = "build/cabac_idr_only.h264"
    raw = "tests/fixtures/scrolling_texture_raw.yuv"
    ffmpeg_out = "build/cabac_idr_ffmpeg.yuv"
    our_out = "build/our_pframe0.yuv"

    if not os.path.exists(fixture):
        print(f"Fixture {fixture} not found. Run the encode step first.")
        sys.exit(1)

    # Decode with ffmpeg
    subprocess.run(["ffmpeg", "-y", "-i", fixture, "-vframes", "1",
                    "-pix_fmt", "yuv420p", ffmpeg_out],
                   capture_output=True, text=True)

    # Decode with our decoder (via test binary, which dumps frame 0)
    # The test binary dumps to build/our_pframe0.yuv for 320x240 fixtures
    # We need to trigger a decode somehow. For now, just compare ffmpeg vs raw.

    # Load raw source
    raw_data = np.frombuffer(open(raw, "rb").read(frame_size), dtype=np.uint8)
    raw_y = raw_data[:w * h].reshape(h, w)

    # Load ffmpeg decode
    ff_data = np.frombuffer(open(ffmpeg_out, "rb").read(frame_size), dtype=np.uint8)
    ff_y = ff_data[:w * h].reshape(h, w)

    # Compute PSNR: ffmpeg vs raw
    diff = np.abs(ff_y.astype(np.int32) - raw_y.astype(np.int32))
    sse = np.sum(diff.astype(np.float64) ** 2)
    mse = sse / (w * h)
    psnr = 10 * np.log10(255.0 ** 2 / mse) if mse > 0 else 999.0

    print(f"ffmpeg decode vs raw source: PSNR={psnr:.1f} dB")
    print(f"  ffmpeg Y mean={ff_y.mean():.1f}, raw Y mean={raw_y.mean():.1f}")

    # If our decoder dump exists, compare that too
    if os.path.exists(our_out):
        our_data = np.frombuffer(open(our_out, "rb").read(frame_size), dtype=np.uint8)
        our_y = our_data[:w * h].reshape(h, w)

        diff2 = np.abs(our_y.astype(np.int32) - raw_y.astype(np.int32))
        sse2 = np.sum(diff2.astype(np.float64) ** 2)
        mse2 = sse2 / (w * h)
        psnr2 = 10 * np.log10(255.0 ** 2 / mse2) if mse2 > 0 else 999.0
        print(f"\nOur decode vs raw source: PSNR={psnr2:.1f} dB")
        print(f"  Our Y mean={our_y.mean():.1f}")

        diff3 = np.abs(our_y.astype(np.int32) - ff_y.astype(np.int32))
        sse3 = np.sum(diff3.astype(np.float64) ** 2)
        mse3 = sse3 / (w * h)
        psnr3 = 10 * np.log10(255.0 ** 2 / mse3) if mse3 > 0 else 999.0
        print(f"Our decode vs ffmpeg: PSNR={psnr3:.1f} dB")
    else:
        print(f"\n{our_out} not found. Run the CABAC test to generate it.")


if __name__ == "__main__":
    main()
