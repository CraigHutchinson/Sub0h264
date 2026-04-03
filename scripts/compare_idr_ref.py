#!/usr/bin/env python3
"""Compare IDR chroma between libavc, ffmpeg, and our decoder.

Checks if reference frame data differs between decoders.

Usage:
    python scripts/compare_idr_ref.py [--col COL] [--row ROW] [--width WIDTH]

Requires: build/libavc_scroll.yuv, build/ffmpeg_scrolling_texture.yuv,
          build/our_idr_chroma.yuv
"""
import argparse
import numpy as np
import os
import sys

W, H = 320, 240
CW, CH = W // 2, H // 2
FRAME_SIZE = W * H * 3 // 2

def load_frame_u(path, frame_idx):
    data = np.fromfile(path, dtype=np.uint8)
    offset = frame_idx * FRAME_SIZE + W * H
    return data[offset:offset + CW * CH].reshape(CH, CW)

def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--col", type=int, default=15, help="Start column to display")
    parser.add_argument("--row", type=int, default=0, help="Start row to display")
    parser.add_argument("--width", type=int, default=10, help="Columns to display")
    parser.add_argument("--rows", type=int, default=3, help="Rows to display")
    args = parser.parse_args()

    decoders = {}
    paths = {
        "libavc": "build/libavc_scroll.yuv",
        "ffmpeg": "build/ffmpeg_scrolling_texture.yuv",
    }
    for name, path in paths.items():
        if os.path.exists(path):
            decoders[name] = load_frame_u(path, 0)
        else:
            print(f"Warning: {path} not found, skipping {name}")

    our_path = "build/our_idr_chroma.yuv"
    if os.path.exists(our_path):
        raw = np.fromfile(our_path, dtype=np.uint8)
        if len(raw) >= CW * CH:
            decoders["ours"] = raw[:CW * CH].reshape(CH, CW)

    if len(decoders) < 2:
        print("Need at least 2 decoder outputs to compare")
        return

    c0, r0 = args.col, args.row
    w, h = args.width, args.rows

    print(f"IDR frame 0 chroma U, cols {c0}-{c0+w-1}, rows {r0}-{r0+h-1}:")
    for name, u in decoders.items():
        print(f"\n  {name}:")
        for r in range(r0, r0 + h):
            vals = list(u[r, c0:c0 + w])
            print(f"    row {r}: {vals}")

    # Overall comparison
    names = list(decoders.keys())
    for i in range(len(names)):
        for j in range(i + 1, len(names)):
            d = np.abs(decoders[names[i]].astype(int) - decoders[names[j]].astype(int))
            print(f"\n{names[i]} vs {names[j]}: max={d.max()}, nonzero={np.count_nonzero(d)}/{CW*CH}")

if __name__ == "__main__":
    main()
