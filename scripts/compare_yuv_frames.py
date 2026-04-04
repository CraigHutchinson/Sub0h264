#!/usr/bin/env python3
"""Compare two YUV I420 frames and report per-MB and per-block error analysis.

Usage:
    python scripts/compare_yuv_frames.py <ours.yuv> <ref.yuv> --width 320 --height 240

Reports PSNR, per-MB max diff, and per-4x4-block error map for the first frame.

SPDX-License-Identifier: MIT
"""
import argparse
import sys

import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("ours", help="Our decoded YUV (I420)")
    parser.add_argument("ref", help="Reference decoded YUV (I420)")
    parser.add_argument("--width", "-W", type=int, default=320)
    parser.add_argument("--height", "-H", type=int, default=240)
    parser.add_argument("--frame", type=int, default=0,
                        help="Frame index to analyze (default: 0)")
    parser.add_argument("--threshold", type=int, default=5,
                        help="Per-pixel diff threshold for reporting blocks (default: 5)")
    parser.add_argument("--max-mbs", type=int, default=20,
                        help="Max MBs to report (default: 20)")
    args = parser.parse_args()

    w, h = args.width, args.height
    frame_size = w * h + 2 * (w // 2) * (h // 2)
    offset = args.frame * frame_size

    our_data = open(args.ours, "rb").read()
    ref_data = open(args.ref, "rb").read()

    if len(our_data) < offset + frame_size:
        print(f"Error: {args.ours} too small for frame {args.frame}", file=sys.stderr)
        sys.exit(1)
    if len(ref_data) < offset + frame_size:
        print(f"Error: {args.ref} too small for frame {args.frame}", file=sys.stderr)
        sys.exit(1)

    our_y = np.frombuffer(our_data[offset:offset + w * h], dtype=np.uint8).reshape(h, w)
    ref_y = np.frombuffer(ref_data[offset:offset + w * h], dtype=np.uint8).reshape(h, w)

    # Overall PSNR
    diff = np.abs(our_y.astype(np.int32) - ref_y.astype(np.int32))
    sse = np.sum(diff.astype(np.float64) ** 2)
    mse = sse / (w * h)
    psnr = 10 * np.log10(255.0 ** 2 / mse) if mse > 0 else 999.0

    print(f"Frame {args.frame}: {w}x{h}")
    print(f"  PSNR:     {psnr:.2f} dB")
    print(f"  Mean diff: {diff.mean():.2f}")
    print(f"  Max diff:  {diff.max()}")
    print(f"  Our Y mean:  {our_y.mean():.1f}  Ref Y mean: {ref_y.mean():.1f}")
    print()

    # Per-MB analysis
    mbs_w = w // 16
    mbs_h = h // 16
    bad_mbs = 0
    for mby in range(mbs_h):
        for mbx in range(mbs_w):
            mb_diff = diff[mby * 16:(mby + 1) * 16, mbx * 16:(mbx + 1) * 16]
            if mb_diff.max() <= args.threshold:
                continue
            bad_mbs += 1
            if bad_mbs <= args.max_mbs:
                print(f"MB({mbx},{mby}): max_diff={mb_diff.max()}, mean={mb_diff.mean():.1f}")
                # Per-4x4 block detail
                for by in range(4):
                    for bx in range(4):
                        b = mb_diff[by * 4:(by + 1) * 4, bx * 4:(bx + 1) * 4]
                        if b.max() > args.threshold:
                            raster_idx = by * 4 + bx
                            print(f"  blk({bx},{by}) raster={raster_idx}: "
                                  f"max={b.max()}, mean={b.mean():.1f}")
    if bad_mbs > args.max_mbs:
        print(f"... and {bad_mbs - args.max_mbs} more bad MBs")
    print(f"\nTotal: {bad_mbs}/{mbs_w * mbs_h} MBs with max diff > {args.threshold}")


if __name__ == "__main__":
    main()
