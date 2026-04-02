"""Check coeff_token VLC tables for ambiguities and compare against spec.

Reads our cCoeffTokenCode/Size tables from cavlc_tables.hpp and
checks for duplicate (code, size) pairs within each nC range table.
"""
import os

# Our tables from cavlc_tables.hpp (nC range 2: 4 <= nC < 8)
# [t1=0..3][tc=0..16]
codes_table2 = [
    [7, 15, 11, 8, 15, 11, 9, 8, 15, 11, 15, 11, 8, 13, 9, 5, 1],   # t1=0
    [0, 7, 10, 9, 10, 6, 12, 8, 14, 10, 14, 10, 14, 13, 9, 5, 1],   # t1=1
    [0, 0, 7, 12, 11, 6, 14, 10, 13, 9, 13, 9, 11, 12, 8, 4, 0],    # t1=2
    [0, 0, 0, 5, 4, 4, 6, 5, 8, 7, 12, 11, 10, 9, 8, 4, 0],        # t1=3
]

sizes_table2 = [
    [4, 6, 6, 6, 7, 7, 7, 7, 8, 8, 9, 9, 9, 10, 10, 10, 10],  # t1=0
    [0, 4, 5, 5, 5, 5, 6, 6, 7, 8, 8, 9, 9, 9, 10, 10, 10],   # t1=1
    [0, 0, 4, 5, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 10],   # t1=2
    [0, 0, 0, 4, 4, 4, 4, 4, 5, 6, 7, 8, 8, 9, 10, 10, 10],   # t1=3
]

print("=== Table 9-5(c) (nC 4-7) ambiguity check ===\n")

# Check for duplicates
entries = []
for t1 in range(4):
    for tc in range(17):
        if t1 > tc:
            continue
        sz = sizes_table2[t1][tc]
        if sz == 0:
            continue
        code = codes_table2[t1][tc]
        entries.append((sz, code, tc, t1))

entries.sort()

print("All entries sorted by (size, code):")
prev = None
for sz, code, tc, t1 in entries:
    dup = ""
    if prev and prev[0] == sz and prev[1] == code:
        dup = " *** DUPLICATE ***"
    print(f"  tc={tc:2d} t1={t1} size={sz:2d} code={code:>{sz}b} ({code:4d}){dup}")
    prev = (sz, code, tc, t1)

# Show specifically the tc=16 entries
print("\n=== tc=16 entries (most likely to be wrong) ===")
for t1 in range(4):
    tc = 16
    sz = sizes_table2[t1][tc]
    code = codes_table2[t1][tc]
    if sz > 0:
        print(f"  tc={tc} t1={t1}: size={sz} code={code:0{sz}b} ({code})")

print("\n=== tc=15 entries ===")
for t1 in range(4):
    tc = 15
    sz = sizes_table2[t1][tc]
    code = codes_table2[t1][tc]
    if sz > 0:
        print(f"  tc={tc} t1={t1}: size={sz} code={code:0{sz}b} ({code})")

print("\n=== tc=14 entries ===")
for t1 in range(4):
    tc = 14
    sz = sizes_table2[t1][tc]
    code = codes_table2[t1][tc]
    if sz > 0:
        print(f"  tc={tc} t1={t1}: size={sz} code={code:0{sz}b} ({code})")

# Per the H.264 spec Table 9-5(c), tc=16 t1=2 and t1=3 both have code 0000000000.
# This is a genuine table property — these codes cannot appear in a valid bitstream
# because tc=16 means all 16 coefficients are non-zero, and having t1=2 vs t1=3
# changes how they're decoded but uses the same prefix.
# The spec resolves this by noting that for tc=16, t1 max is 3, and codes
# are assigned as:
#   t1=0: 0000000001 (=1)
#   t1=1: 0000000001 (=1) -- SAME as t1=0!
#   t1=2: 0000000000 (=0)
#   t1=3: 0000000000 (=0) -- SAME as t1=2!
#
# The table in the spec IS ambiguous for tc>=15 in some nC ranges.
# Correct decoders resolve this by picking the HIGHEST t1 for the matched code
# (since trailing ones are a more efficient encoding).
print("\n=== Analysis ===")
print("tc=16 has duplicate codes in Table 9-5(c).")
print("Correct resolution: for ambiguous (code, size), pick LARGEST t1.")
print("Our decoder picks SMALLEST t1 (first match in iteration order).")
print("This may cause wrong trailing ones count, affecting level decode.")
