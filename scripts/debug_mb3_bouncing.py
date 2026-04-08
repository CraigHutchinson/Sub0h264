#!/usr/bin/env python3
"""Debug bouncing ball MB(3,0) — compare our output vs ffmpeg at pixel level.

Decodes frame 0 with both our decoder (via YUV dump) and ffmpeg,
then shows per-4x4-block mean error for the first 2 MB rows.

Usage:
    python scripts/debug_mb3_bouncing.py

SPDX-License-Identifier: MIT
"""
import subprocess
import struct
import sys
import os
import numpy as np

WIDTH = 320
HEIGHT = 240
MB_SIZE = 16

FIXTURE = "tests/fixtures/bouncing_ball_ionly.h264"
RAW_SRC = "tests/fixtures/bouncing_ball_ionly_raw.yuv"


def decode_with_ffmpeg(h264_path, width, height):
    """Decode first frame with ffmpeg to raw YUV."""
    cmd = [
        "ffmpeg", "-y", "-i", h264_path,
        "-vframes", "1", "-pix_fmt", "yuv420p",
        "-f", "rawvideo", "pipe:1"
    ]
    result = subprocess.run(cmd, capture_output=True)
    if result.returncode != 0:
        print(f"ffmpeg error: {result.stderr.decode()}", file=sys.stderr)
        sys.exit(1)
    return result.stdout


def decode_with_ours(h264_path, width, height):
    """Decode first frame with our trace tool (dumps YUV)."""
    trace_exe = "build/test_apps/trace/Release/sub0h264_trace.exe"
    if not os.path.exists(trace_exe):
        print(f"Trace tool not found at {trace_exe}", file=sys.stderr)
        sys.exit(1)
    dump_path = "build/debug_mb3_ours.yuv"
    cmd = [trace_exe, h264_path, "--dump", dump_path, "--frame", "0"]
    result = subprocess.run(cmd, capture_output=True)
    if result.returncode != 0:
        print(f"Our decoder error: {result.stderr.decode()}", file=sys.stderr)
        # Try without --frame flag
        cmd = [trace_exe, h264_path, "--dump", dump_path]
        result = subprocess.run(cmd, capture_output=True)
    if os.path.exists(dump_path):
        with open(dump_path, "rb") as f:
            return f.read()
    return None


def load_yuv_y(data, width, height):
    """Extract Y plane from I420 YUV data."""
    y_size = width * height
    if len(data) < y_size:
        print(f"YUV data too small: {len(data)} < {y_size}")
        return None
    y = np.frombuffer(data[:y_size], dtype=np.uint8).reshape(height, width)
    return y


def main():
    # Load raw source
    with open(RAW_SRC, "rb") as f:
        raw_data = f.read()
    raw_y = load_yuv_y(raw_data, WIDTH, HEIGHT)

    # Decode with ffmpeg
    ff_data = decode_with_ffmpeg(FIXTURE, WIDTH, HEIGHT)
    ff_y = load_yuv_y(ff_data, WIDTH, HEIGHT)

    # Decode with our decoder
    our_data = decode_with_ours(FIXTURE, WIDTH, HEIGHT)
    our_y = load_yuv_y(our_data, WIDTH, HEIGHT) if our_data else None

    if raw_y is None or ff_y is None:
        print("Failed to load frames")
        return

    # Compare ffmpeg vs raw source
    print("=== ffmpeg vs raw source (Y plane) ===")
    diff_ff = np.abs(ff_y.astype(np.int16) - raw_y.astype(np.int16))
    mse_ff = np.mean(diff_ff.astype(np.float64) ** 2)
    psnr_ff = 10 * np.log10(255**2 / mse_ff) if mse_ff > 0 else 999
    print(f"  Overall Y PSNR: {psnr_ff:.2f} dB")

    if our_y is not None:
        print("\n=== Our decoder vs raw source (Y plane) ===")
        diff_ours = np.abs(our_y.astype(np.int16) - raw_y.astype(np.int16))
        mse_ours = np.mean(diff_ours.astype(np.float64) ** 2)
        psnr_ours = 10 * np.log10(255**2 / mse_ours) if mse_ours > 0 else 999
        print(f"  Overall Y PSNR: {psnr_ours:.2f} dB")

        print("\n=== Our decoder vs ffmpeg (Y plane) — per-MB diff ===")
        diff_vs_ff = np.abs(our_y.astype(np.int16) - ff_y.astype(np.int16))
        mbs_x = WIDTH // MB_SIZE
        mbs_y = HEIGHT // MB_SIZE
        # Show first 2 rows
        for mby in range(min(2, mbs_y)):
            for mbx in range(mbs_x):
                blk = diff_vs_ff[mby*16:(mby+1)*16, mbx*16:(mbx+1)*16]
                mean_err = np.mean(blk)
                max_err = np.max(blk)
                if max_err > 0:
                    print(f"  MB({mbx},{mby}): mean={mean_err:.1f} max={max_err}")
                else:
                    print(f"  MB({mbx},{mby}): PERFECT")

        # Show per-4x4 block detail for MB(3,0) and MB(4,0)
        for mbx_detail in [2, 3, 4, 5]:
            mby_detail = 0
            print(f"\n  MB({mbx_detail},{mby_detail}) per-4x4 block detail:")
            for blk_row in range(4):
                for blk_col in range(4):
                    py = mby_detail * 16 + blk_row * 4
                    px = mbx_detail * 16 + blk_col * 4
                    our_block = our_y[py:py+4, px:px+4]
                    ff_block = ff_y[py:py+4, px:px+4]
                    raw_block = raw_y[py:py+4, px:px+4]
                    d = our_block.astype(np.int16) - ff_block.astype(np.int16)
                    if np.any(d != 0):
                        print(f"    blk[{blk_row},{blk_col}] diff: min={d.min()} max={d.max()} mean={d.mean():.1f}")
                        # Show actual pixel values for first differing block
                        if abs(d).max() > 2:
                            print(f"      ours:  {our_block.flatten().tolist()}")
                            print(f"      ffmpeg:{ff_block.flatten().tolist()}")
                            print(f"      raw:   {raw_block.flatten().tolist()}")
    else:
        print("\nCould not decode with our decoder (trace tool)")
        print("Falling back to analyzing ffmpeg vs raw only")

        # Show per-MB PSNR for first row
        print("\n=== ffmpeg vs raw — per-MB PSNR (row 0) ===")
        for mbx in range(WIDTH // MB_SIZE):
            blk = raw_y[0:16, mbx*16:(mbx+1)*16].astype(np.float64)
            ff_blk = ff_y[0:16, mbx*16:(mbx+1)*16].astype(np.float64)
            mse = np.mean((blk - ff_blk)**2)
            psnr = 10 * np.log10(255**2 / mse) if mse > 0 else 999
            print(f"  MB({mbx},0): PSNR={psnr:.1f} dB")


if __name__ == "__main__":
    main()
