#!/usr/bin/env python3
"""Verify our CABAC table against libavc's gau4_ih264_cabac_table.

Extracts both tables and compares entry-by-entry. Reports any mismatches.

Usage:
    python scripts/verify_cabac_table.py

SPDX-License-Identifier: MIT
"""
import re
import sys


def extract_table(path, var_name):
    """Extract a uint32 [128][4] table from C source."""
    with open(path) as f:
        content = f.read()

    # Find the table definition
    idx = content.find(var_name)
    if idx < 0:
        print(f"Table '{var_name}' not found in {path}", file=sys.stderr)
        return None

    # Find the opening brace of the 2D array
    brace_start = content.index("{", idx)

    # Extract row-by-row: each row is { n, n, n, n }
    table = []
    pos = brace_start + 1
    while len(table) < 128:
        # Find next inner brace pair
        row_start = content.find("{", pos)
        row_end = content.find("}", row_start)
        if row_start < 0 or row_end < 0:
            break
        row_text = content[row_start + 1:row_end]
        nums = re.findall(r"\d+", row_text)
        if len(nums) >= 4:
            table.append([int(nums[0]), int(nums[1]), int(nums[2]), int(nums[3])])
        pos = row_end + 1

    return table


def main():
    ours = extract_table("components/sub0h264/src/cabac.hpp", "cCabacTable")
    libavc = extract_table("docs/reference/libavc/common/ih264_cabac_tables.c",
                           "gau4_ih264_cabac_table")

    if not ours or not libavc:
        sys.exit(1)

    print(f"Our table: {len(ours)} rows")
    print(f"libavc table: {len(libavc)} rows")

    mismatches = 0
    for row in range(min(len(ours), len(libavc))):
        for col in range(4):
            if ours[row][col] != libavc[row][col]:
                pState = row & 63
                mps = row >> 6
                our_lps = ours[row][col] & 0xFF
                lib_lps = libavc[row][col] & 0xFF
                print(f"  MISMATCH row {row} (p={pState},mps={mps}) col {col}: "
                      f"ours={ours[row][col]} libavc={libavc[row][col]} "
                      f"(lps: {our_lps} vs {lib_lps})")
                mismatches += 1

    if mismatches == 0:
        print("All entries match!")

        # Show specific entry for debugging
        row77 = ours[77]
        print(f"\nRow 77 (p=13, mps=1): {row77}")
        for col in range(4):
            lps = row77[col] & 0xFF
            mps_next = (row77[col] >> 8) & 0x7F
            lps_next = (row77[col] >> 15) & 0x7F
            print(f"  qRange={col}: rangeLPS={lps}, nextMPS={mps_next}, nextLPS={lps_next}")
    else:
        print(f"\n{mismatches} mismatches found!")


if __name__ == "__main__":
    main()
