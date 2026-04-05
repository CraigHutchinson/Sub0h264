#!/usr/bin/env python3
"""Invert the IDCT to recover 4x4 coefficients from pixel residuals.

Given prediction and output, computes the dequantized coefficients
that the IDCT must have processed.

Usage:
    python scripts/invert_block_residual.py

SPDX-License-Identifier: MIT
"""
import numpy as np


def forward_dct4x4(residual):
    """Forward 4x4 integer DCT (inverse of the inverse).
    Matches H.264 §8.5.12 butterfly but forward direction.
    """
    tmp = np.zeros((4, 4), dtype=np.int64)
    # Horizontal
    for i in range(4):
        s = residual[i]
        x0 = s[0] + s[3]
        x1 = s[0] - s[3]
        x2 = s[1] + s[2]
        x3 = s[1] - s[2]
        tmp[i, 0] = x0 + x2
        tmp[i, 1] = x1 * 2 + x3
        tmp[i, 2] = x0 - x2
        tmp[i, 3] = x1 - x3 * 2

    out = np.zeros((4, 4), dtype=np.int64)
    # Vertical
    for j in range(4):
        s = tmp[:, j]
        x0 = s[0] + s[3]
        x1 = s[0] - s[3]
        x2 = s[1] + s[2]
        x3 = s[1] - s[2]
        out[0, j] = x0 + x2
        out[1, j] = x1 * 2 + x3
        out[2, j] = x0 - x2
        out[3, j] = x1 - x3 * 2
    return out


def main():
    # Single-MB test, scan4 (raster 2) at absX=8, absY=0
    # Mode=Horizontal, prediction = left column repeated
    pred = np.array([[74, 74, 74, 74],
                     [74, 74, 74, 74],
                     [30, 30, 30, 30],
                     [30, 30, 30, 30]], dtype=np.int64)

    # Our output
    our_out = np.array([[63, 63, 63, 63],
                        [64, 64, 64, 64],
                        [0, 0, 0, 0],
                        [0, 0, 0, 0]], dtype=np.int64)

    # ffmpeg output
    ff_out = np.array([[41, 41, 41, 41],
                       [42, 42, 42, 42],
                       [22, 22, 22, 22],
                       [22, 22, 22, 22]], dtype=np.int64)

    our_residual = our_out - pred
    ff_residual = ff_out - pred

    print("Our residual:")
    print(our_residual)

    print("\nffmpeg residual:")
    print(ff_residual)

    # The IDCT output is: residual = (IDCT(coeffs) + 32) >> 6
    # So: IDCT(coeffs) ≈ residual << 6 - 32
    # But the forward DCT gives us the coefficients directly.

    # Approximate: scale residual back to coefficient domain
    # residual * 64 ≈ IDCT(coeffs)... but this isn't exact due to the
    # butterfly normalization. Let me just compute the forward DCT.

    our_coeffs_approx = forward_dct4x4(our_residual)
    ff_coeffs_approx = forward_dct4x4(ff_residual)

    print("\nApprox forward DCT of our residual (÷ should recover dequant):")
    print(our_coeffs_approx)

    print("\nApprox forward DCT of ffmpeg residual:")
    print(ff_coeffs_approx)

    print("\nDifference (our - ffmpeg) in transform domain:")
    print(our_coeffs_approx - ff_coeffs_approx)

    # Our known dequant: [-1296, 0, 0, 0, 736, 0, 0, 0, 0, 0, 0, 0, -276, 0, 0, 0]
    print("\nOur known dequant (raster order):")
    print("[-1296, 0, 0, 0, 736, 0, 0, 0, 0, 0, 0, 0, -276, 0, 0, 0]")


if __name__ == "__main__":
    main()
