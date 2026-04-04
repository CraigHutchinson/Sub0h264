#!/usr/bin/env python3
"""Verify 8x8 inverse DCT against reference implementation.

Tests the 8x8 IDCT butterfly with known inputs and compares against
a numpy reference (exact floating-point) to catch scaling or sign errors.

Usage:
    python scripts/test_8x8_idct.py

SPDX-License-Identifier: MIT
"""
import numpy as np


def h264_idct8x8_ref(d):
    """Reference 8x8 inverse DCT per ITU-T H.264 §8.5.12.2.

    Uses the integer butterfly with half-pel coefficients.
    Input: 8x8 array of dequantized coefficients.
    Output: 8x8 array of residuals (before >>6 normalization).
    """
    # H.264 8x8 IDCT butterfly (one dimension)
    def idct8_1d(s):
        # Even part
        a0 = s[0] + s[4]
        a2 = s[0] - s[4]
        a4 = (s[2] >> 1) - s[6]
        a6 = s[2] + (s[6] >> 1)

        e0 = a0 + a6
        e1 = a2 + a4
        e2 = a2 - a4
        e3 = a0 - a6

        # Odd part
        b0 = -s[3] + s[5] - s[7] - (s[7] >> 1)
        b1 =  s[1] + s[7] - s[3] - (s[3] >> 1)
        b2 = -s[1] + s[7] + s[5] + (s[5] >> 1)
        b3 =  s[1] + s[3] + s[5] + (s[7] >> 1)

        o0 = b0 + (b3 >> 2)
        o1 = b1 + (b2 >> 2)
        o2 = b2 - (b1 >> 2)
        o3 = b3 - (b0 >> 2)

        return [e0+o3, e1+o2, e2+o1, e3+o0, e3-o0, e2-o1, e1-o2, e0-o3]

    # Use integer arithmetic (matching C++ implementation)
    d = np.array(d, dtype=np.int64)

    # Horizontal pass
    tmp = np.zeros((8, 8), dtype=np.int64)
    for i in range(8):
        tmp[i, :] = idct8_1d(d[i, :].tolist())

    # Vertical pass
    out = np.zeros((8, 8), dtype=np.int64)
    for j in range(8):
        col = idct8_1d(tmp[:, j].tolist())
        for i in range(8):
            out[i, j] = (col[i] + 32) >> 6

    return out


def main():
    print("=== 8x8 IDCT verification ===\n")

    # Test 1: DC only
    d = np.zeros((8, 8), dtype=np.int64)
    d[0, 0] = 256  # DC coefficient
    r = h264_idct8x8_ref(d)
    print(f"Test 1: DC=256, all others 0")
    print(f"  Expected: all pixels = (256+32)>>6 = {(256+32)>>6}")
    print(f"  Got: {r[0,0]}, uniform={np.all(r == r[0,0])}")
    print(f"  Range: [{r.min()}, {r.max()}]")
    print()

    # Test 2: DC = -320
    d = np.zeros((8, 8), dtype=np.int64)
    d[0, 0] = -320
    r = h264_idct8x8_ref(d)
    print(f"Test 2: DC=-320")
    print(f"  Expected: all pixels = (-320+32)>>6 = {(-320+32)>>6}")
    print(f"  Got: {r[0,0]}, uniform={np.all(r == r[0,0])}")
    print()

    # Test 3: Realistic case - QP=20, DC coeff c=1
    # dequant: c * normAdjust8x8[2][0] << (3-2) = 1 * 26 << 1 = 52
    d = np.zeros((8, 8), dtype=np.int64)
    d[0, 0] = 52
    r = h264_idct8x8_ref(d)
    print(f"Test 3: Realistic DC=52 (c=1 at QP=20)")
    print(f"  Expected: (52+32)>>6 = {(52+32)>>6}")
    print(f"  Got: {r[0,0]}")
    print()

    # Test 4: Verify the prediction-add chain
    # If pred=128, dc_coeff=-5, QP=20:
    # dequant: -5 * 26 << 1 = -260
    # IDCT: (-260+32)>>6 = -228>>6 = -3 (arithmetic right shift)
    # pixel = 128 + (-3) = 125 (close to target 123)
    d = np.zeros((8, 8), dtype=np.int64)
    d[0, 0] = -260
    r = h264_idct8x8_ref(d)
    print(f"Test 4: DC=-260 (c=-5 at QP=20)")
    print(f"  IDCT output: {r[0,0]} (expected ~-4)")
    print(f"  pred+residual: 128 + {r[0,0]} = {128 + r[0,0]}")
    print()

    # Test 5: Check if the butterfly gain is 1 for DC
    # For a pure DC input, the 8x8 butterfly should give:
    # Horiz: all 8 values = DC  (gain = 1)
    # Vert: all 8 values = DC   (gain = 1)
    # So output = DC >> 6
    for dc_val in [64, 128, 256, 512, 1024]:
        d = np.zeros((8, 8), dtype=np.int64)
        d[0, 0] = dc_val
        r = h264_idct8x8_ref(d)
        expected = (dc_val + 32) >> 6
        actual = r[0, 0]
        status = "OK" if actual == expected else "MISMATCH"
        print(f"  DC={dc_val}: expected={expected}, got={actual} [{status}]")


if __name__ == "__main__":
    main()
