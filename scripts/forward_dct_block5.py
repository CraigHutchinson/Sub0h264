#!/usr/bin/env python3
"""Compute forward DCT to find expected coefficients for MB(3,0) scan block 5.

Uses the known ffmpeg output and our prediction to determine what the
correct dequantized coefficients should be, then compares against our decode.

Usage:
    python scripts/forward_dct_block5.py

SPDX-License-Identifier: MIT
"""
import numpy as np

# H.264 4x4 forward integer DCT (matching the inverse in the spec)
def forward_dct4x4(residual):
    """H.264 forward 4x4 integer DCT to approximate pre-scaling coefficients."""
    r = residual.astype(np.int64)
    # Horizontal
    tmp = np.zeros((4,4), dtype=np.int64)
    for i in range(4):
        s = r[i]
        a = s[0] + s[3]
        b = s[0] - s[3]
        c = s[1] + s[2]
        d = s[1] - s[2]
        tmp[i,0] = a + c
        tmp[i,1] = 2*b + d
        tmp[i,2] = a - c
        tmp[i,3] = b - 2*d
    # Vertical
    out = np.zeros((4,4), dtype=np.int64)
    for j in range(4):
        s = tmp[:,j]
        a = s[0] + s[3]
        b = s[0] - s[3]
        c = s[1] + s[2]
        d = s[1] - s[2]
        out[0,j] = a + c
        out[1,j] = 2*b + d
        out[2,j] = a - c
        out[3,j] = b - 2*d
    return out

# H.264 4x4 inverse integer DCT
def inverse_dct4x4(coeffs):
    """H.264 inverse 4x4 integer DCT per §8.5.12."""
    c = coeffs.astype(np.int64)
    # Horizontal (row transform)
    tmp = np.zeros((4,4), dtype=np.int64)
    for i in range(4):
        s = c[i]
        e = s[0] + s[2]
        f = s[0] - s[2]
        g = (s[1] >> 1) - s[3]
        h = s[1] + (s[3] >> 1)
        tmp[i,0] = e + h
        tmp[i,1] = f + g
        tmp[i,2] = f - g
        tmp[i,3] = e - h
    # Vertical (column transform)
    out = np.zeros((4,4), dtype=np.int64)
    for j in range(4):
        s = tmp[:,j]
        e = s[0] + s[2]
        f = s[0] - s[2]
        g = (s[1] >> 1) - s[3]
        h = s[1] + (s[3] >> 1)
        out[0,j] = (e + h + 32) >> 6
        out[1,j] = (f + g + 32) >> 6
        out[2,j] = (f - g + 32) >> 6
        out[3,j] = (e - h + 32) >> 6
    return out


def main():
    # Our prediction for block 5 (horizontal mode, constant per row)
    pred = np.array([
        [149, 149, 149, 149],
        [160, 160, 160, 160],
        [107, 107, 107, 107],
        [190, 190, 190, 190],
    ], dtype=np.int64)

    # Our output (from trace)
    our_out = np.array([
        [71, 74, 102, 175],
        [148, 184, 70, 116],
        [139, 64, 63, 114],
        [171, 193, 183, 217],
    ], dtype=np.int64)

    # ffmpeg output (from pixel comparison)
    ff_out = np.array([
        [74, 77, 105, 178],
        [140, 176, 62, 108],
        [184, 109, 108, 159],
        [133, 155, 145, 179],
    ], dtype=np.int64)

    # Pixel diffs (ours - ffmpeg)
    diff = our_out - ff_out
    print("Output diff (ours - ffmpeg):")
    print(diff)
    print()

    # Our dequantized coefficients (from trace, in raster order)
    our_dq = np.array([
        [-1368, -276, 864, -920],
        [-1380, -464, 0, -464],
        [0, -1748, -216, 184],
        [-92, -1044, 920, 1160],
    ], dtype=np.int64)

    # Verify our IDCT of our dequant coefficients
    our_idct = inverse_dct4x4(our_dq)
    print("Our IDCT output (residual):")
    print(our_idct)
    print()

    # What the correct residual should be (ffmpeg output - our prediction)
    # Note: prediction should be the same since blocks 0-4 are perfect,
    # and horizontal prediction just uses the left column.
    # But I only have pred rows 0 and 1 from the trace. Let me compute
    # the expected residual from ffmpeg output.
    #
    # Actually, the prediction is the same for both decoders (same mode,
    # same reference samples since prior blocks are perfect).
    # So: correct_residual = ff_out - pred
    # And: our_residual = our_out - pred
    # Diff: our_residual - correct_residual = our_out - ff_out = diff

    # The residual comes from IDCT(dequant_coeffs).
    # So the IDCT output diff = diff (what we already have).

    print("If pred is the same, IDCT diff = output diff:")
    print(diff)
    print()

    # Forward-transform the diff to find what coefficient error causes this
    fwd_diff = forward_dct4x4(diff)
    print("Forward DCT of output diff (shows which coefficients are wrong):")
    print(fwd_diff)
    print()

    # The forward DCT of the per-row constant error pattern should show
    # errors only in column 0 of the coefficient matrix.
    # Column 0 in raster: positions (0,0), (1,0), (2,0), (3,0)
    print("Column 0 of forward DCT (should be the only non-zero):")
    print(f"  (0,0)={fwd_diff[0,0]}  (1,0)={fwd_diff[1,0]}  (2,0)={fwd_diff[2,0]}  (3,0)={fwd_diff[3,0]}")

    # Check what raw coefficient error this corresponds to at QP=17
    qp = 17
    qp_div6 = qp // 6  # = 2
    qp_mod6 = qp % 6   # = 5
    # Scale factors from standard table
    dequant_scale = [10, 11, 13, 14, 16, 18]  # Per QP%6, position (0,0)
    scale = dequant_scale[qp_mod6]  # = 18

    # For position (0,0): dequant = raw * scale * (1 << qp_div6)
    # So: raw = dequant / (scale * (1 << qp_div6))
    factor = scale * (1 << qp_div6)  # = 18 * 4 = 72
    print(f"\nQP={qp}, scale[{qp_mod6}][0]={scale}, shift=1<<{qp_div6}={1<<qp_div6}, factor={factor}")
    print(f"Coefficient error at (0,0) in fwd domain: {fwd_diff[0,0]}")
    print(f"  This corresponds to raw coeff error of: {fwd_diff[0,0] / factor:.2f}")

    # Show all dequant scale factors for this QP
    # Position-dependent scales from Table 8-15
    v_scale = [
        [10, 16, 13],
        [11, 18, 14],
        [13, 20, 16],
        [14, 23, 18],
        [16, 25, 20],
        [18, 29, 23],
    ]
    print(f"\nDequant scales for QP={qp} (qpMod6={qp_mod6}):")
    for r in range(4):
        for c in range(4):
            # Scale index: (r%2)*2 + (c%2) → 0=even/even, 1=even/odd, 2=odd/even, 3=odd/odd
            # Actually the pattern is: positions where both row,col are even use scale[0],
            # positions where row XOR col is 1 use scale[2], positions where both odd use scale[1]
            if r % 2 == 0 and c % 2 == 0:
                s = v_scale[qp_mod6][0]
            elif r % 2 == 1 and c % 2 == 1:
                s = v_scale[qp_mod6][1]
            else:
                s = v_scale[qp_mod6][2]
            dq = s * (1 << qp_div6)
            if fwd_diff[r, c] != 0:
                raw_err = fwd_diff[r, c] / dq
                print(f"  ({r},{c}): scale={s} dq_factor={dq} fwd_diff={fwd_diff[r,c]} raw_err={raw_err:.2f}")


if __name__ == "__main__":
    main()
