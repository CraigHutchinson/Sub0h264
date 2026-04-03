#!/usr/bin/env python3
"""Compare chroma MC output for a specific MB partition against manual bilinear.

Usage:
    python scripts/check_chroma_mc.py [--mbx MBX] [--mby MBY] [--part PART] [--mvx MVX] [--mvy MVY]

Defaults to MB(0,0) partition 1 with MV=(22,6).

Requires: numpy, ffmpeg decoded output at build/ffmpeg_scrolling_texture.yuv
"""
import argparse
import numpy as np
import sys

W, H = 320, 240
CW, CH = W // 2, H // 2
FRAME_SIZE = W * H * 3 // 2

def load_yuv_u(path, frame_idx):
    data = np.fromfile(path, dtype=np.uint8)
    offset = frame_idx * FRAME_SIZE + W * H
    return data[offset:offset + CW * CH].reshape(CH, CW)

def clamp(v, lo, hi):
    return max(lo, min(hi, v))

def bilinear_mc(ref_u, ref_x, ref_y, cdx, cdy, width, height):
    """Chroma bilinear MC — ITU-T H.264 §8.4.2.2.2."""
    w00, w10 = (8 - cdx) * (8 - cdy), cdx * (8 - cdy)
    w01, w11 = (8 - cdx) * cdy, cdx * cdy
    result = np.zeros((height, width), dtype=np.uint8)
    for r in range(height):
        for c in range(width):
            x, y = ref_x + c, ref_y + r
            a = int(ref_u[clamp(y, 0, CH-1), clamp(x, 0, CW-1)])
            b = int(ref_u[clamp(y, 0, CH-1), clamp(x+1, 0, CW-1)])
            cc = int(ref_u[clamp(y+1, 0, CH-1), clamp(x, 0, CW-1)])
            d = int(ref_u[clamp(y+1, 0, CH-1), clamp(x+1, 0, CW-1)])
            result[r, c] = (w00 * a + w10 * b + w01 * cc + w11 * d + 32) >> 6
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mbx", type=int, default=0)
    parser.add_argument("--mby", type=int, default=0)
    parser.add_argument("--part", type=int, default=1, help="Partition index (0=left/top, 1=right/bottom)")
    parser.add_argument("--mvx", type=int, default=22, help="MV x in quarter-pel luma")
    parser.add_argument("--mvy", type=int, default=6, help="MV y in quarter-pel luma")
    parser.add_argument("--parttype", choices=["8x16", "16x8", "16x16"], default="8x16")
    parser.add_argument("--ffmpeg", default="build/ffmpeg_scrolling_texture.yuv")
    args = parser.parse_args()

    ref_u = load_yuv_u(args.ffmpeg, 0)  # IDR frame 0
    out_u = load_yuv_u(args.ffmpeg, 1)  # P-frame 1

    # Chroma block origin for this partition
    if args.parttype == "8x16":
        cx = args.mbx * 8 + args.part * 4  # right half for part 1
        cy = args.mby * 8
        pw, ph = 4, 8
    elif args.parttype == "16x8":
        cx = args.mbx * 8
        cy = args.mby * 8 + args.part * 4
        pw, ph = 8, 4
    else:
        cx, cy = args.mbx * 8, args.mby * 8
        pw, ph = 8, 8

    # Chroma MV derivation — §8.4.2.2.2
    ref_x = cx + (args.mvx >> 3)
    ref_y = cy + (args.mvy >> 3)
    cdx = args.mvx & 7
    cdy = args.mvy & 7

    print(f"MB({args.mbx},{args.mby}) part={args.part} ({args.parttype}) MV=({args.mvx},{args.mvy})")
    print(f"Chroma: origin=({cx},{cy}) refPos=({ref_x},{ref_y}) frac=({cdx},{cdy})")
    weights = ((8-cdx)*(8-cdy), cdx*(8-cdy), (8-cdx)*cdy, cdx*cdy)
    print(f"Weights: {weights} (sum={sum(weights)})")

    # IDR reference near the MC position
    print(f"\nIDR ref U near ({ref_x},{ref_y}):")
    for r in range(min(3, ph+1)):
        yr = clamp(ref_y + r, 0, CH - 1)
        vals = [ref_u[yr, clamp(ref_x + c, 0, CW - 1)] for c in range(pw + 1)]
        print(f"  row {ref_y+r}: {vals}")

    # Manual MC
    mc = bilinear_mc(ref_u, ref_x, ref_y, cdx, cdy, pw, ph)

    # ffmpeg output
    ffmpeg_block = out_u[cy:cy+ph, cx:cx+pw]

    # Comparison — show all rows
    diff = np.abs(mc.astype(int) - ffmpeg_block.astype(int))
    mismatches = np.count_nonzero(diff)
    print(f"\nManual MC vs ffmpeg (all {ph} rows):")
    for r in range(ph):
        marker = " *" if np.any(diff[r]) else ""
        print(f"  row {r}: manual={list(mc[r])}  ffmpeg={list(ffmpeg_block[r])}{marker}")
    print(f"Mismatches: {mismatches}/{pw*ph}")

    if mismatches > 0:
        print(f"Max diff: {diff.max()}, Mean diff: {diff.mean():.2f}")

if __name__ == "__main__":
    main()
