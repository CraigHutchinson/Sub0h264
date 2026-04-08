#!/usr/bin/env python3
"""Compare our YUV frame 0 vs ffmpeg pixel-by-pixel for bouncing ball.

Shows per-4x4-block error for each MB, highlighting where errors begin.

Usage:
    python scripts/compare_mb_pixels.py build/debug_bb.yuv build/debug_bb_ff.yuv 320 240

SPDX-License-Identifier: MIT
"""
import sys
import numpy as np


def main():
    if len(sys.argv) < 5:
        print(f"Usage: {sys.argv[0]} ours.yuv ffmpeg.yuv width height")
        sys.exit(1)

    ours_path = sys.argv[1]
    ff_path = sys.argv[2]
    width = int(sys.argv[3])
    height = int(sys.argv[4])
    y_size = width * height

    with open(ours_path, "rb") as f:
        ours_data = f.read(y_size)
    with open(ff_path, "rb") as f:
        ff_data = f.read(y_size)

    ours_y = np.frombuffer(ours_data, dtype=np.uint8).reshape(height, width)
    ff_y = np.frombuffer(ff_data, dtype=np.uint8).reshape(height, width)

    diff = ours_y.astype(np.int16) - ff_y.astype(np.int16)

    mbs_x = width // 16
    mbs_y = height // 16

    print("=== Per-MB Y diff: our decoder vs ffmpeg (frame 0) ===\n")

    first_error_mb = None
    for mby in range(min(3, mbs_y)):
        for mbx in range(mbs_x):
            blk = diff[mby*16:(mby+1)*16, mbx*16:(mbx+1)*16]
            max_err = np.max(np.abs(blk))
            if max_err == 0:
                print(f"  MB({mbx},{mby}): PERFECT")
            else:
                mean_err = np.mean(np.abs(blk))
                print(f"  MB({mbx},{mby}): mean_abs={mean_err:.1f}  max_abs={max_err}")
                if first_error_mb is None:
                    first_error_mb = (mbx, mby)

    if first_error_mb is None:
        print("\nAll MBs in first 3 rows are pixel-perfect!")
        return

    # Show per-4x4 detail for the first few error MBs
    mbx0, mby0 = first_error_mb
    for mbx in range(max(0, mbx0-1), min(mbs_x, mbx0+3)):
        mby = mby0
        print(f"\n--- MB({mbx},{mby}) per-4x4 block detail ---")
        for br in range(4):
            for bc in range(4):
                py = mby * 16 + br * 4
                px = mbx * 16 + bc * 4
                d = diff[py:py+4, px:px+4]
                if np.any(d != 0):
                    print(f"  4x4[{br},{bc}] (px {px},{py}): diff range [{d.min()},{d.max()}]")
                    # Show pixel-level detail
                    for row in range(4):
                        ours_row = ours_y[py+row, px:px+4].tolist()
                        ff_row = ff_y[py+row, px:px+4].tolist()
                        d_row = d[row].tolist()
                        print(f"    row{row}: ours={ours_row} ff={ff_row} diff={d_row}")
                else:
                    print(f"  4x4[{br},{bc}] (px {px},{py}): PERFECT")

    # Check if errors are systematic — same offset per block?
    print(f"\n--- Error pattern analysis for MB({mbx0},{mby0}) ---")
    mb_diff = diff[mby0*16:(mby0+1)*16, mbx0*16:(mbx0+1)*16]
    unique_diffs = np.unique(mb_diff)
    print(f"  Unique diff values: {unique_diffs.tolist()}")
    for val in unique_diffs:
        if val != 0:
            count = np.sum(mb_diff == val)
            print(f"  diff={val}: {count} pixels")


if __name__ == "__main__":
    main()
