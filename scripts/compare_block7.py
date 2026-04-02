"""Compare our decoded pixels against ffmpeg reference for specific MB blocks.

Usage: python scripts/compare_block7.py [mbx] [mby]
Default: MB(7,7) — center of the frame, I_4x4 area.
"""
import argparse
import os
import sys

import numpy as np


def load_y_plane(path, w=640, h=480):
    return np.fromfile(path, dtype=np.uint8)[:w*h].reshape(h, w)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mbx", type=int, nargs="?", default=7)
    parser.add_argument("mby", type=int, nargs="?", default=7)
    args = parser.parse_args()

    project = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ours = load_y_plane(os.path.join(project, "build", "tests", "Release",
                                     "debug_baseline_frame0.yuv"))
    ref = load_y_plane(os.path.join(project, "build", "frame_compare", "ref.yuv"))

    mbx, mby = args.mbx, args.mby
    x0, y0 = mbx * 16, mby * 16

    print(f"MB({mbx},{mby}) — Y-plane pixel comparison (16x16)")
    print()

    our_block = ours[y0:y0+16, x0:x0+16]
    ref_block = ref[y0:y0+16, x0:x0+16]
    diff_block = our_block.astype(np.int16) - ref_block.astype(np.int16)

    print("Our decode:")
    for row in range(16):
        vals = " ".join(f"{our_block[row, col]:3d}" for col in range(16))
        print(f"  y={y0+row:3d}: {vals}")

    print()
    print("FFmpeg reference:")
    for row in range(16):
        vals = " ".join(f"{ref_block[row, col]:3d}" for col in range(16))
        print(f"  y={y0+row:3d}: {vals}")

    print()
    print("Difference (ours - ref):")
    for row in range(16):
        vals = " ".join(f"{diff_block[row, col]:+4d}" for col in range(16))
        print(f"  y={y0+row:3d}: {vals}")

    print()
    print(f"Stats: max_abs_diff={np.abs(diff_block).max()}, "
          f"mean_abs_diff={np.abs(diff_block).mean():.1f}, "
          f"nonzero={np.count_nonzero(diff_block)}/256")


if __name__ == "__main__":
    main()
