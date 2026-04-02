#!/usr/bin/env python3
"""Analyze a decoded YUV frame to find decode coverage and failure points.

Usage:
    python scripts/analyze_decode.py [yuv_file] [--width W] [--height H]

Defaults to debug_baseline_frame0.yuv in the project root.
"""
import argparse
import os
import sys

import numpy as np


def analyze(yuv_path, width, height):
    if not os.path.isfile(yuv_path):
        print(f"ERROR: {yuv_path} not found", file=sys.stderr)
        sys.exit(1)

    y = np.fromfile(yuv_path, dtype=np.uint8)[:width * height].reshape(height, width)
    mb_w, mb_h = width // 16, height // 16

    # Find last non-zero row
    last_nz_row = -1
    for row in range(height - 1, -1, -1):
        if y[row].max() > 0:
            last_nz_row = row
            break

    total_nz = np.count_nonzero(y)
    print(f"Frame: {width}x{height} ({mb_w}x{mb_h} MBs)")
    print(f"Non-zero pixels: {total_nz}/{width*height} ({100*total_nz/(width*height):.1f}%)")
    print(f"Last non-zero row: {last_nz_row} (MB row {last_nz_row//16})")
    print()

    # Find first all-zero MB (decode stop point)
    stop_mbx, stop_mby, stop_addr = -1, -1, -1
    for mby in range(mb_h):
        for mbx in range(mb_w):
            block = y[mby*16:(mby+1)*16, mbx*16:(mbx+1)*16]
            if np.count_nonzero(block) == 0:
                stop_mbx, stop_mby = mbx, mby
                stop_addr = mby * mb_w + mbx
                break
        if stop_mbx >= 0:
            break

    if stop_addr >= 0:
        print(f"First all-zero MB: ({stop_mbx},{stop_mby}) addr={stop_addr}")
        total_decoded = stop_addr
        print(f"MBs decoded: {total_decoded}/{mb_w*mb_h} ({100*total_decoded/(mb_w*mb_h):.1f}%)")
    else:
        print(f"All MBs have non-zero content (full decode)")
        total_decoded = mb_w * mb_h

    print()

    # Per-MB-row summary
    print("Per-MB-row summary:")
    for mby in range(mb_h):
        row_data = y[mby*16:(mby+1)*16, :]
        nz = np.count_nonzero(row_data)
        if nz == 0:
            print(f"  MB row {mby:2d} (y={mby*16:3d}-{mby*16+15:3d}): all zero")
            break

        # Count fully-decoded vs partial MBs
        full = 0
        partial = 0
        zero = 0
        for mbx in range(mb_w):
            block = y[mby*16:(mby+1)*16, mbx*16:(mbx+1)*16]
            bnz = np.count_nonzero(block)
            if bnz == 256:
                full += 1
            elif bnz > 0:
                partial += 1
            else:
                zero += 1

        print(f"  MB row {mby:2d}: {full:2d} full, {partial:2d} partial, {zero:2d} zero")

    # Detail around the stop point
    if stop_addr >= 0 and stop_mby > 0:
        print()
        print(f"Detail around stop (MB row {stop_mby}):")
        for mbx in range(max(0, stop_mbx - 3), min(mb_w, stop_mbx + 4)):
            block = y[stop_mby*16:(stop_mby+1)*16, mbx*16:(mbx+1)*16]
            nz = np.count_nonzero(block)
            mx = block.max()
            marker = " <-- STOP" if mbx == stop_mbx else ""
            print(f"  MB({mbx},{stop_mby}): {nz:3d}/256 non-zero, max={mx:3d}{marker}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    project = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    default_yuv = os.path.join(project, "debug_baseline_frame0.yuv")

    parser.add_argument("yuv_file", nargs="?", default=default_yuv,
                        help="Path to I420 YUV file")
    parser.add_argument("--width", type=int, default=640)
    parser.add_argument("--height", type=int, default=480)
    args = parser.parse_args()

    analyze(args.yuv_file, args.width, args.height)


if __name__ == "__main__":
    main()
