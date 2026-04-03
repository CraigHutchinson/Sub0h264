#!/usr/bin/env python3
"""Compare a decoded frame dump against ffmpeg and raw source.

Usage:
    python scripts/compare_frame.py --frame 16 --fixture pan_up
    python scripts/compare_frame.py --frame 8 --fixture scrolling_texture

Expects:
    build/our_pframe{N}.yuv     — our decoder dump (run quality test first)
    build/ffmpeg_{fixture}.yuv  — ffmpeg decode (auto-generated if missing)
    tests/fixtures/{fixture}_raw.yuv — raw uncompressed source

Reports Y/U/V PSNR vs both ffmpeg and raw, plus per-row chroma analysis.
"""
import argparse
import math
import os
import subprocess
import sys

import numpy as np

W, H = 320, 240
CW, CH = W // 2, H // 2
FRAME_SIZE = W * H * 3 // 2


def load_plane(path, frame_idx, plane):
    """Load a Y/U/V plane from a multi-frame YUV file."""
    data = np.fromfile(path, dtype=np.uint8)
    y_off = frame_idx * FRAME_SIZE
    if plane == "Y":
        return data[y_off:y_off + W * H].reshape(H, W)
    u_off = y_off + W * H
    if plane == "U":
        return data[u_off:u_off + CW * CH].reshape(CH, CW)
    v_off = u_off + CW * CH
    return data[v_off:v_off + CW * CH].reshape(CH, CW)


def psnr(a, b):
    a, b = a.astype(np.int32), b.astype(np.int32)
    sse = np.sum((a - b) ** 2)
    n = a.size
    if sse == 0:
        return 99.0
    return 10 * math.log10(255 * 255 * n / sse)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--frame", type=int, required=True)
    parser.add_argument("--fixture", type=str, required=True)
    parser.add_argument("--rows", type=int, default=5,
                        help="Number of chroma error rows to show")
    args = parser.parse_args()

    our_path = f"build/our_pframe{args.frame}.yuv"
    ff_path = f"build/ffmpeg_{args.fixture}.yuv"
    raw_path = f"tests/fixtures/{args.fixture}_raw.yuv"
    h264_path = f"tests/fixtures/{args.fixture}.h264"

    if not os.path.exists(our_path):
        print(f"Error: {our_path} not found. Run the quality test first.")
        return

    # Generate ffmpeg reference if missing
    if not os.path.exists(ff_path):
        if os.path.exists(h264_path):
            print(f"Generating ffmpeg reference: {ff_path}")
            subprocess.run(["ffmpeg", "-y", "-i", h264_path, "-pix_fmt", "yuv420p",
                            ff_path], capture_output=True)
        else:
            print(f"Error: {h264_path} not found.")
            return

    our_dump = np.fromfile(our_path, dtype=np.uint8)
    if our_dump.size < FRAME_SIZE:
        print(f"Error: dump too small ({our_dump.size} < {FRAME_SIZE})")
        return

    # Load planes
    our_y = our_dump[:W * H].reshape(H, W)
    our_u = our_dump[W * H:W * H + CW * CH].reshape(CH, CW)
    our_v = our_dump[W * H + CW * CH:W * H + 2 * CW * CH].reshape(CH, CW)

    print(f"=== Frame {args.frame} of {args.fixture} ===\n")

    # Compare vs ffmpeg
    if os.path.exists(ff_path):
        ff_y = load_plane(ff_path, args.frame, "Y")
        ff_u = load_plane(ff_path, args.frame, "U")
        ff_v = load_plane(ff_path, args.frame, "V")
        print("vs ffmpeg:")
        for name, a, b in [("Y", our_y, ff_y), ("U", our_u, ff_u), ("V", our_v, ff_v)]:
            diff = np.abs(a.astype(int) - b.astype(int))
            print(f"  {name}: PSNR={psnr(a, b):.2f} dB  max={diff.max()}  "
                  f"nonzero={np.count_nonzero(diff)}/{a.size}")
        print()

    # Compare vs raw source
    if os.path.exists(raw_path):
        raw_y = load_plane(raw_path, args.frame, "Y")
        raw_u = load_plane(raw_path, args.frame, "U")
        raw_v = load_plane(raw_path, args.frame, "V")
        print("vs raw source (ground truth):")
        for name, a, b in [("Y", our_y, raw_y), ("U", our_u, raw_u), ("V", our_v, raw_v)]:
            diff = np.abs(a.astype(int) - b.astype(int))
            print(f"  {name}: PSNR={psnr(a, b):.2f} dB  max={diff.max()}  "
                  f"nonzero={np.count_nonzero(diff)}/{a.size}")
        print()

    # Chroma row analysis vs ffmpeg
    if os.path.exists(ff_path):
        ff_u = load_plane(ff_path, args.frame, "U")
        diff_u = np.abs(our_u.astype(int) - ff_u.astype(int))
        shown = 0
        print("Chroma U rows with errors vs ffmpeg:")
        for r in range(CH):
            max_d = diff_u[r].max()
            nz = np.count_nonzero(diff_u[r])
            if max_d > 3:
                print(f"  row {r:3d} (MB row {r // 8:2d}): max={max_d:3d}  "
                      f"diffs={nz:3d}/{CW}")
                shown += 1
                if shown >= args.rows:
                    remaining = sum(1 for rr in range(r + 1, CH)
                                    if diff_u[rr].max() > 3)
                    if remaining:
                        print(f"  ... {remaining} more rows with errors")
                    break


if __name__ == "__main__":
    main()
