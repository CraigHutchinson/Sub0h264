#!/usr/bin/env python3
"""Verify CAVLC level placement for MB(3,0) scan block 5.

Given the known levels and output coefficients, work backwards to
find what run_before values were used, and verify correctness.

Usage:
    python scripts/verify_level_placement.py

SPDX-License-Identifier: MIT
"""

ZIGZAG = [0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15]

def main():
    # From trace: call #84
    tc = 14
    to = 0
    tz = 2
    levels = [10, 10, 2, -4, -3, -9, -1, -19, -10, 12, -4, -15, -3, -19]
    # Output raster coefficients
    raster_coeffs = [-19, -3, 12, -10, -15, -4, 0, -4, 0, -19, -3, 2, -1, -9, 10, 10]

    # Reverse-engineer the run_before values from the placement
    # coeffIdx starts at tc + tz - 1 = 14 + 2 - 1 = 15
    coeffIdx = tc + tz - 1  # = 15

    print(f"tc={tc} to={to} tz={tz}")
    print(f"levels (reverse scan): {levels}")
    print(f"Starting coeffIdx: {coeffIdx}")
    print()

    # For each level, find the zigzag position it was placed at
    # by checking which raster position has the matching value
    zerosLeft = tz
    placements = []

    for i in range(tc):
        zigPos = coeffIdx
        rasterPos = ZIGZAG[zigPos] if zigPos < 16 else -1
        actual_val = raster_coeffs[rasterPos] if rasterPos >= 0 else None

        # Determine run_before
        if i < tc - 1 and zerosLeft > 0:
            # Need to figure out run from the gap
            # For now, just try run=0 and run=1 etc
            pass

        placements.append({
            'level_idx': i,
            'level_val': levels[i],
            'zigzag_pos': zigPos,
            'raster_pos': rasterPos,
            'actual_at_raster': actual_val,
            'match': actual_val == levels[i] if actual_val is not None else False
        })

        # For proper reconstruction, we'd need the actual run_before values.
        # Let me just verify the final result matches.
        print(f"  level[{i:2d}]={levels[i]:4d} -> zigzag[{zigPos:2d}]"
              f" -> raster[{rasterPos:2d}] (actual={actual_val})"
              f" {'OK' if actual_val == levels[i] else 'WRONG'}")

        # Move to next position (assume run=0 for non-last, run=zerosLeft for last)
        if i < tc - 1:
            coeffIdx -= 1  # Assume run=0 for now
        # For proper decode, run would be read from bitstream

    # Now let me properly simulate the placement with run_before
    print("\n=== Proper placement simulation ===")
    coeffs = [0] * 16
    coeffIdx = tc + tz - 1  # = 15
    zerosLeft = tz

    # To find the correct run_before values, I need to place levels such that
    # the output matches raster_coeffs.
    # Build a map: raster_pos -> value for non-zero coefficients
    nonzero_raster = {}
    for r, v in enumerate(raster_coeffs):
        if v != 0:
            nonzero_raster[r] = v

    # Build reverse map: zigzag index -> raster position
    # Find which zigzag positions the non-zero values occupy
    # Then determine what run_before values produce that assignment

    # Target: levels placed at zigzag positions that map to the correct raster positions
    target_zigzag = []
    for r, v in enumerate(raster_coeffs):
        if v != 0:
            # Find zigzag index that maps to raster r
            for z in range(16):
                if ZIGZAG[z] == r:
                    target_zigzag.append(z)
                    break

    target_zigzag.sort(reverse=True)
    print(f"Target zigzag positions (desc): {target_zigzag}")
    print(f"Levels:                         {levels}")

    # Verify: levels placed at target_zigzag positions should match raster_coeffs
    test_coeffs = [0] * 16
    for i, z in enumerate(target_zigzag):
        rp = ZIGZAG[z]
        test_coeffs[rp] = levels[i]
    print(f"Reconstructed raster: {test_coeffs}")
    print(f"Expected raster:      {raster_coeffs}")
    print(f"Match: {test_coeffs == raster_coeffs}")

    # Now compute run_before values from the zigzag positions
    print("\n=== Run-before values ===")
    coeffIdx = tc + tz - 1  # = 15
    zerosLeft = tz  # = 2
    runs = []
    for i in range(tc):
        expected_zigzag = target_zigzag[i]
        run = coeffIdx - expected_zigzag
        runs.append(run)
        print(f"  i={i}: coeffIdx={coeffIdx} target_zigzag={expected_zigzag} run={run} level={levels[i]} zerosLeft={zerosLeft}")
        zerosLeft -= run
        coeffIdx = expected_zigzag - 1

    print(f"\nRun-before values: {runs}")
    print(f"Sum of runs: {sum(runs)} (should equal totalZeros={tz})")


if __name__ == "__main__":
    main()
