#!/usr/bin/env python3
"""Find the exact first pixel that differs between our decode and ffmpeg.

Scans row by row, column by column, to find the first differing pixel
in the non-deblocked output.

Usage:
    python scripts/find_first_pixel_diff.py

SPDX-License-Identifier: MIT
"""
import numpy as np

W, H = 320, 240


def main():
    our = np.frombuffer(open("build/debug_bb_nodeblock.yuv", "rb").read()[:W*H],
                        dtype=np.uint8).reshape(H, W)
    ff = np.frombuffer(open("build/debug_bb_ff_nodeblock.yuv", "rb").read()[:W*H],
                       dtype=np.uint8).reshape(H, W)

    print("Searching for first differing pixel (no deblock)...")
    for y in range(H):
        for x in range(W):
            if our[y, x] != ff[y, x]:
                mbx, mby = x // 16, y // 16
                bx, by = (x % 16) // 4, (y % 16) // 4
                print(f"FIRST DIFF at pixel ({x},{y}) = MB({mbx},{mby}) block({bx},{by})")
                print(f"  ours={our[y,x]} ffmpeg={ff[y,x]} diff={int(our[y,x])-int(ff[y,x])}")

                # Show surrounding context
                print(f"\n  Pixel context around ({x},{y}):")
                for dy in range(-1, 5):
                    yy = y + dy
                    if 0 <= yy < H:
                        row_ours = [int(our[yy, x+dx]) for dx in range(-2, 6) if 0 <= x+dx < W]
                        row_ff = [int(ff[yy, x+dx]) for dx in range(-2, 6) if 0 <= x+dx < W]
                        row_diff = [a-b for a, b in zip(row_ours, row_ff)]
                        marker = " <--" if dy == 0 else ""
                        print(f"    y={yy}: ours={row_ours} ff={row_ff} diff={row_diff}{marker}")
                return

    print("No differences found!")


if __name__ == "__main__":
    main()
