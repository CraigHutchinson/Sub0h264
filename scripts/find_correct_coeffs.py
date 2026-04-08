#!/usr/bin/env python3
"""Find correct dequant coefficients for MB(3,0) scan block 5.

Implements the exact H.264 4x4 integer IDCT (matching our C++ code)
and searches for column-0 coefficient corrections that eliminate
the pixel error.

Usage:
    python scripts/find_correct_coeffs.py

SPDX-License-Identifier: MIT
"""
import numpy as np


def idct4x4_exact(coeffs, pred):
    """Exact H.264 4x4 integer IDCT matching our C++ inverseDct4x4AddPred.
    coeffs: 4x4 int array (raster order)
    pred: 4x4 uint8 array
    Returns: 4x4 uint8 output
    """
    c = coeffs.astype(np.int64)
    p = pred.astype(np.int64)

    # Horizontal transform → transposed intermediate
    tmp = np.zeros((4, 4), dtype=np.int64)
    for i in range(4):
        q0, q1, q2, q3 = c[i, 0], c[i, 1], c[i, 2], c[i, 3]
        x0 = q0 + q2
        x1 = q0 - q2
        # Python integer >> for negative: use arithmetic shift
        x2 = (q1 >> 1 if q1 >= 0 else -((-q1) >> 1)) - q3
        x3 = q1 + (q3 >> 1 if q3 >= 0 else -((-q3) >> 1))
        # Store transposed
        tmp[0, i] = x0 + x3
        tmp[1, i] = x1 + x2
        tmp[2, i] = x1 - x2
        tmp[3, i] = x0 - x3

    # Vertical transform + add pred + clip
    out = np.zeros((4, 4), dtype=np.int64)
    for j in range(4):
        t0, t1, t2, t3 = tmp[j, 0], tmp[j, 1], tmp[j, 2], tmp[j, 3]
        x0 = t0 + t2
        x1 = t0 - t2
        x2 = (t1 >> 1 if t1 >= 0 else -((-t1) >> 1)) - t3
        x3 = t1 + (t3 >> 1 if t3 >= 0 else -((-t3) >> 1))

        out[0, j] = np.clip(p[0, j] + ((x0 + x3 + 32) >> 6), 0, 255)
        out[1, j] = np.clip(p[1, j] + ((x1 + x2 + 32) >> 6), 0, 255)
        out[2, j] = np.clip(p[2, j] + ((x1 - x2 + 32) >> 6), 0, 255)
        out[3, j] = np.clip(p[3, j] + ((x0 - x3 + 32) >> 6), 0, 255)

    return out.astype(np.uint8)


# Use C-style arithmetic right shift
def asr(val, n):
    """Arithmetic right shift matching C behavior."""
    if val >= 0:
        return val >> n
    else:
        return -((-val) >> n) - (1 if (-val) & ((1 << n) - 1) else 0)
    # Actually: Python int >> IS arithmetic for negative integers!
    # -5 >> 1 = -3 (floor division) but we need C's behavior which is -(5>>1)=-2
    # Wait: C arithmetic right shift: -5 >> 1 = -3 (sign-extends)
    # Python: -5 >> 1 = -3 as well (Python uses floor division for >>)
    # So they match! Let me verify: -1380 >> 1
    # Python: -1380 >> 1 = -690  (correct, same as C)
    # -92 >> 1 = -46 (correct)
    # OK, Python's >> matches C's arithmetic right shift.


def main():
    # Our dequant coefficients (raster order, from trace)
    our_dq = np.array([
        [-1368, -276, 864, -920],
        [-1380, -464, 0, -464],
        [0, -1748, -216, 184],
        [-92, -1044, 920, 1160],
    ], dtype=np.int64)

    # Prediction (horizontal mode, constant per row)
    pred = np.array([
        [149, 149, 149, 149],
        [160, 160, 160, 160],
        [107, 107, 107, 107],
        [190, 190, 190, 190],
    ], dtype=np.uint8)

    # Our output (verify IDCT matches)
    our_expected = np.array([
        [71, 74, 102, 175],
        [148, 184, 70, 116],
        [139, 64, 63, 114],
        [171, 193, 183, 217],
    ], dtype=np.uint8)

    our_result = idct4x4_exact(our_dq, pred)
    print("Our IDCT result:")
    print(our_result)
    print("Expected (from decoder):")
    print(our_expected)
    print("Match:", np.array_equal(our_result, our_expected))

    # ffmpeg output (the correct answer)
    ff_expected = np.array([
        [74, 77, 105, 178],
        [140, 176, 62, 108],
        [184, 109, 108, 159],
        [133, 155, 145, 179],
    ], dtype=np.uint8)

    # Raw coefficients (pre-dequant)
    our_raw = np.array([
        [-19, -3, 12, -10],
        [-15, -4, 0, -4],
        [0, -19, -3, 2],
        [-1, -9, 10, 10],
    ], dtype=np.int64)

    # Dequant scale factors at QP=17 (qpMod6=5, qpDiv6=2)
    # Position (r,c) scale depends on parity
    v_scale = np.array([
        [18, 23, 18, 23],
        [23, 29, 23, 29],
        [18, 23, 18, 23],
        [23, 29, 23, 29],
    ], dtype=np.int64)
    qp_shift = 4  # 1 << qpDiv6 = 1 << 2 = 4

    # Verify dequant: raw * scale * shift
    dq_check = our_raw * v_scale * qp_shift
    print("\nDequant verification (raw * scale * shift):")
    print(dq_check)
    print("Our dequant (from trace):")
    print(our_dq)
    print("Dequant match:", np.array_equal(dq_check, our_dq))

    # Now try corrections to column 0 raw coefficients
    # Search for raw corrections that make the IDCT produce ffmpeg's output
    print("\n=== Searching for correct column-0 raw coefficients ===")

    # We know the error is in column 0. Try corrections of -10..+10 for each position.
    for d0 in range(-5, 6):
        for d1 in range(-5, 6):
            for d2 in range(-20, 21):  # Wider range since (2,0) raw=0
                for d3 in range(-10, 11):
                    trial_raw = our_raw.copy()
                    trial_raw[0, 0] += d0
                    trial_raw[1, 0] += d1
                    trial_raw[2, 0] += d2
                    trial_raw[3, 0] += d3

                    trial_dq = trial_raw * v_scale * qp_shift
                    trial_out = idct4x4_exact(trial_dq, pred)

                    if np.array_equal(trial_out, ff_expected):
                        print(f"FOUND! Corrections: d0={d0} d1={d1} d2={d2} d3={d3}")
                        print(f"Correct raw column 0: [{trial_raw[0,0]}, {trial_raw[1,0]}, {trial_raw[2,0]}, {trial_raw[3,0]}]")
                        print(f"Our raw column 0:     [{our_raw[0,0]}, {our_raw[1,0]}, {our_raw[2,0]}, {our_raw[3,0]}]")
                        return

    print("No exact column-0-only match found")

    # Now try ALL 16 positions with ±3 range
    print("\n=== Full 16-coefficient search (±3 each) ===")
    # Too many combos for brute force (7^16). Try a greedy approach instead.

    # Start with our coefficients. For each position, try ±1,2,3 and keep the
    # change that best reduces the max error.
    best_raw = our_raw.copy()
    best_dq = best_raw * v_scale * qp_shift
    best_out = idct4x4_exact(best_dq, pred)
    best_max_err = np.max(np.abs(best_out.astype(np.int16) - ff_expected.astype(np.int16)))
    print(f"Starting max_err: {best_max_err}")

    for iteration in range(20):
        improved = False
        for r in range(4):
            for c in range(4):
                for delta in [-1, 1, -2, 2, -3, 3, -4, 4, -5, 5]:
                    trial = best_raw.copy()
                    trial[r, c] += delta
                    trial_dq = trial * v_scale * qp_shift
                    trial_out = idct4x4_exact(trial_dq, pred)
                    trial_err = np.max(np.abs(trial_out.astype(np.int16) - ff_expected.astype(np.int16)))
                    if trial_err < best_max_err:
                        best_raw = trial
                        best_dq = trial_dq
                        best_out = trial_out
                        best_max_err = trial_err
                        improved = True
                        if trial_err == 0:
                            break
                if best_max_err == 0:
                    break
            if best_max_err == 0:
                break
        if best_max_err == 0 or not improved:
            break

    print(f"Best max_err after greedy: {best_max_err}")
    if best_max_err < best_max_err + 1:
        diff_raw = best_raw - our_raw
        print(f"Coefficient corrections needed:")
        for r in range(4):
            for c in range(4):
                if diff_raw[r, c] != 0:
                    print(f"  ({r},{c}): {our_raw[r,c]} -> {best_raw[r,c]} (delta={diff_raw[r,c]})")
        print(f"Corrected output:")
        print(best_out)
        print(f"Expected (ffmpeg):")
        print(ff_expected)


if __name__ == "__main__":
    main()
