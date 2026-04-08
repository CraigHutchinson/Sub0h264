#!/usr/bin/env python3
"""Diff our decoder trace against libavc trace to find first divergence.

Aligns blocks by MB using known bit ranges, converts libavc's sparse
format to raster order, and compares coefficient by coefficient.

Usage:
    python scripts/diff_traces.py build/trace_sub0h264.txt build/trace_libavc_full.txt

SPDX-License-Identifier: MIT
"""
import sys
import re


ZIGZAG = [0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15]

# MB(0) residual: libavc bits 73..2368, MB(1): 2427..4746, etc.
# These come from the LIBAVC-RES traces.
MB_RANGES = [
    (73, 2368),     # MB(0,0)
    (2427, 4746),   # MB(1,0)
    (4790, 7073),   # MB(2,0)
    (7120, 9376),   # MB(3,0)
]


def levels_to_raster(levels, sig_map_hex):
    """Convert libavc sparse levels + sig_coeff_map to raster-order coefficients.
    libavc stores levels in DECREASING scan position order.
    """
    sig_map = int(sig_map_hex, 16)
    set_positions = []
    for zigzag_pos in range(15, -1, -1):
        if sig_map & (1 << zigzag_pos):
            set_positions.append(zigzag_pos)

    coeffs = [0] * 16
    for i, zigzag_pos in enumerate(set_positions):
        if i < len(levels):
            coeffs[ZIGZAG[zigzag_pos]] = levels[i]
    return coeffs


def parse_ours(filename):
    """Parse our trace: extract RAW lines with coefficients."""
    blocks = []
    with open(filename) as f:
        for line in f:
            m = re.match(r'RAW (\d+) (\d+) scan=(\d+) nC=(\d+) tc=(\d+) bits=(\d+) crc=\w+ coeffs=\[(.+)\]', line)
            if m:
                mbx, mby, scan, nc, tc, bits, coeffs_str = m.groups()
                coeffs = [int(x) for x in coeffs_str.split()]
                blocks.append({
                    'mbx': int(mbx), 'mby': int(mby), 'scan': int(scan),
                    'nc': int(nc), 'tc': int(tc), 'bits': int(bits),
                    'coeffs': coeffs
                })
    return blocks


def parse_libavc(filename):
    """Parse libavc COEFF trace."""
    blocks = []
    with open(filename) as f:
        for line in f:
            m = re.search(r'tc=(\d+) to=(\d+) tz=(\d+) map=([0-9a-f]+) levels=\[(.+?)\] bit=(\d+)', line)
            if m:
                tc, to, tz, sig_map, levels_str, bit = m.groups()
                levels = [int(x) for x in levels_str.split()]
                blocks.append({
                    'tc': int(tc), 'to': int(to), 'tz': int(tz),
                    'sig_map': sig_map, 'levels': levels, 'bit': int(bit)
                })
    return blocks


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} our_trace.txt libavc_trace.txt")
        sys.exit(1)

    ours = parse_ours(sys.argv[1])
    all_libavc = parse_libavc(sys.argv[2])

    # Only use first frame's libavc data (filter by bit range for first 4 MBs)
    # First frame MB(0) starts at bit ~73 and last MB ends at ~230K
    # Use first occurrence of each bit position (libavc decodes multiple frames)
    seen_bits = set()
    libavc = []
    for lb in all_libavc:
        if lb['bit'] not in seen_bits and lb['bit'] < 100000:
            seen_bits.add(lb['bit'])
            libavc.append(lb)

    print(f"Our trace: {len(ours)} luma blocks (first row only)")
    print(f"libavc trace: {len(libavc)} blocks (first frame, deduplicated)")

    # For each MB, extract libavc blocks by bit range
    for mb_idx, (start_bit, end_bit) in enumerate(MB_RANGES):
        mb_libavc = [lb for lb in libavc if start_bit <= lb['bit'] < end_bit]
        mb_ours = [ob for ob in ours if ob['mbx'] == mb_idx]

        # Convert libavc blocks to raster coefficients
        libavc_rasters = []
        for lb in mb_libavc:
            raster = levels_to_raster(lb['levels'], lb['sig_map'])
            libavc_rasters.append((lb, raster))

        print(f"\n=== MB({mb_idx},0): {len(mb_ours)} our blocks, {len(mb_libavc)} libavc blocks ===")

        # Match our blocks to libavc blocks by coefficients
        for ob in mb_ours:
            found = False
            for lb, raster in libavc_rasters:
                if raster == ob['coeffs']:
                    found = True
                    break

            if found:
                print(f"  scan={ob['scan']:2d} tc={ob['tc']:2d} bits={ob['bits']:3d}: MATCH (libavc bit={lb['bit']})")
            else:
                print(f"  scan={ob['scan']:2d} tc={ob['tc']:2d} bits={ob['bits']:3d}: *** NO MATCH ***")
                print(f"    our coeffs: {ob['coeffs']}")
                # Show closest libavc block
                for lb, raster in libavc_rasters:
                    if lb['tc'] == ob['tc']:
                        ndiff = sum(1 for a, b in zip(ob['coeffs'], raster) if a != b)
                        if ndiff <= 8:
                            print(f"    close libavc (tc={lb['tc']} bit={lb['bit']} ndiff={ndiff}): {raster}")


if __name__ == "__main__":
    main()
