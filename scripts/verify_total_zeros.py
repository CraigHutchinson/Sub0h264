#!/usr/bin/env python3
"""Verify cTotalZerosSize/Code tables against ITU-T H.264 Table 9-7.

Checks every entry in the total_zeros VLC table for correctness.
Reports any mismatches against the spec.

Reference: ITU-T H.264 Table 9-7 (total_zeros for 4x4 blocks)
"""

# ITU-T H.264 Table 9-7: total_zeros VLC codes for 4x4 blocks.
# Indexed by [totalCoeff-1][total_zeros] = (code_value, code_length)
# totalCoeff ranges from 1 to 15. For totalCoeff=N, total_zeros ranges from 0 to 16-N.
SPEC_TABLE_9_7 = {
    1: [(1,1),(3,3),(2,3),(3,4),(2,4),(3,5),(2,5),(3,6),(2,6),(3,7),(2,7),(3,8),(2,8),(3,9),(2,9),(1,9)],
    2: [(7,3),(6,3),(5,3),(4,3),(3,3),(5,4),(4,4),(3,4),(2,4),(3,5),(2,5),(3,6),(2,6),(1,6),(0,6)],
    3: [(5,4),(7,3),(6,3),(5,3),(4,4),(3,4),(4,3),(3,3),(2,4),(3,5),(2,5),(1,6),(1,5),(0,6)],
    4: [(3,5),(7,3),(5,4),(4,4),(6,3),(5,3),(4,3),(3,4),(3,3),(2,4),(2,5),(1,5),(0,5)],
    5: [(5,4),(4,4),(3,4),(7,3),(6,3),(5,3),(4,3),(3,3),(2,4),(1,5),(1,4),(0,5)],
    6: [(1,6),(1,5),(7,3),(6,3),(5,3),(4,3),(3,3),(2,3),(1,4),(1,3),(0,6)],
    7: [(1,6),(1,5),(5,3),(4,3),(3,3),(3,2),(2,3),(1,4),(1,3),(0,6)],
    8: [(1,6),(1,4),(1,5),(3,3),(3,2),(2,2),(2,3),(1,3),(0,6)],
    9: [(1,6),(0,6),(1,4),(3,2),(2,2),(1,3),(1,2),(1,5)],
    10: [(1,5),(0,5),(1,3),(3,2),(2,2),(1,2),(1,4)],
    11: [(0,4),(1,4),(1,3),(2,3),(1,1),(3,3)],
    12: [(0,4),(1,4),(1,2),(1,1),(1,3)],
    13: [(0,3),(1,3),(1,1),(1,2)],
    14: [(0,2),(1,2),(1,1)],
    15: [(0,1),(1,1)],
}

def main():
    # Read our table values from cavlc_tables.hpp
    # These are copied from the source file
    tzIndex = [0, 16, 31, 45, 58, 70, 81, 91, 100, 108, 115, 121, 126, 130, 133]
    tzSize = [1, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 9,
              3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6, 6, 6, 6,
              4, 3, 3, 3, 4, 4, 3, 3, 4, 5, 5, 6, 5, 6,
              5, 3, 4, 4, 3, 3, 3, 4, 3, 4, 5, 5, 5,
              4, 4, 4, 3, 3, 3, 3, 3, 4, 5, 4, 5,
              6, 5, 3, 3, 3, 3, 3, 3, 4, 3, 6,
              6, 5, 3, 3, 3, 2, 3, 4, 3, 6,
              6, 4, 5, 3, 2, 2, 3, 3, 6,
              6, 6, 4, 2, 2, 3, 2, 5,
              5, 5, 3, 2, 2, 2, 4,
              4, 4, 3, 3, 1, 3,
              4, 4, 2, 1, 3,
              3, 3, 1, 2,
              2, 2, 1,
              1, 1]
    tzCode = [1, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 1,
              7, 6, 5, 4, 3, 5, 4, 3, 2, 3, 2, 3, 2, 1, 0,
              5, 7, 6, 5, 4, 3, 4, 3, 2, 3, 2, 1, 1, 0,
              3, 7, 5, 4, 6, 5, 4, 3, 3, 2, 2, 1, 0,
              5, 4, 3, 7, 6, 5, 4, 3, 2, 1, 1, 0,
              1, 1, 7, 6, 5, 4, 3, 2, 1, 1, 0,
              1, 1, 5, 4, 3, 3, 2, 1, 1, 0,
              1, 1, 1, 3, 3, 2, 2, 1, 0,
              1, 0, 1, 3, 2, 1, 1, 1,
              1, 0, 1, 3, 2, 1, 1,
              0, 1, 1, 2, 1, 3,
              0, 1, 1, 1, 1,
              0, 1, 1, 1,
              0, 1, 1,
              0, 1]

    errors = 0
    for tc in range(1, 16):
        spec = SPEC_TABLE_9_7[tc]
        start = tzIndex[tc - 1]
        num_tz = len(spec)

        for tz in range(num_tz):
            spec_code, spec_size = spec[tz]
            our_size = tzSize[start + tz]
            our_code = tzCode[start + tz]

            if our_size != spec_size or our_code != spec_code:
                print(f"  MISMATCH tc={tc} tz={tz}: spec=({spec_code},{spec_size}b) "
                      f"ours=({our_code},{our_size}b)")
                errors += 1

    if errors == 0:
        print("All total_zeros entries match ITU-T H.264 Table 9-7")
    else:
        print(f"\n{errors} total mismatches found")

if __name__ == "__main__":
    main()
