#!/usr/bin/env python3
"""Compare Y, U, V planes separately between two YUV files.

Usage:
    python scripts/compare_yuv_planes.py <ours.yuv> <ref.yuv> --width 320 --height 240

SPDX-License-Identifier: MIT
"""
import argparse
import numpy as np


def psnr(a, b):
    diff = a.astype(np.float64) - b.astype(np.float64)
    mse = np.mean(diff ** 2)
    return 10 * np.log10(255.0 ** 2 / mse) if mse > 0 else 999.0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("ours")
    parser.add_argument("ref")
    parser.add_argument("--width", "-W", type=int, default=320)
    parser.add_argument("--height", "-H", type=int, default=240)
    args = parser.parse_args()

    w, h = args.width, args.height
    y_size = w * h
    uv_size = (w // 2) * (h // 2)

    our = np.frombuffer(open(args.ours, "rb").read(y_size + 2 * uv_size), dtype=np.uint8)
    ref = np.frombuffer(open(args.ref, "rb").read(y_size + 2 * uv_size), dtype=np.uint8)

    our_y = our[:y_size]
    our_u = our[y_size:y_size + uv_size]
    our_v = our[y_size + uv_size:]
    ref_y = ref[:y_size]
    ref_u = ref[y_size:y_size + uv_size]
    ref_v = ref[y_size + uv_size:]

    print(f"Y plane: PSNR={psnr(our_y, ref_y):.2f} dB, "
          f"mean_diff={np.mean(np.abs(our_y.astype(int) - ref_y.astype(int))):.2f}, "
          f"max_diff={np.max(np.abs(our_y.astype(int) - ref_y.astype(int)))}")
    print(f"U plane: PSNR={psnr(our_u, ref_u):.2f} dB, "
          f"mean_diff={np.mean(np.abs(our_u.astype(int) - ref_u.astype(int))):.2f}, "
          f"max_diff={np.max(np.abs(our_u.astype(int) - ref_u.astype(int)))}")
    print(f"V plane: PSNR={psnr(our_v, ref_v):.2f} dB, "
          f"mean_diff={np.mean(np.abs(our_v.astype(int) - ref_v.astype(int))):.2f}, "
          f"max_diff={np.max(np.abs(our_v.astype(int) - ref_v.astype(int)))}")


if __name__ == "__main__":
    main()
