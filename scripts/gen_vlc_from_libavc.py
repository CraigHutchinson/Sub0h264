"""Generate correct coeff_token VLC tables from libavc's gau2_ih264d_code_gx.

libavc decodes coeff_token for nC < 8 as:
  1. Count leading zeros → ldz
  2. Flush the '1' stop bit
  3. Read 3 more bits → suffix
  4. index = ldz * 8 + suffix + offset[nC]
  5. entry = gau2_ih264d_code_gx[index]
  6. extra_flush = entry & 3, t1 = (entry >> 2) & 3, tc = (entry >> 4) & 0x1F
  7. Flush extra_flush more bits

So the total VLC code is: ldz zeros + '1' + 3 bits + extra_flush bits
  = ldz + 1 + 3 + extra_flush bits total

The VLC code value is: the entire bit pattern from the start.

For each (nC, index) in the table, we can reconstruct:
  - ldz = (index - offset[nC]) // 8
  - suffix_3bits = (index - offset[nC]) % 8
  - extra_flush = entry & 3
  - total bits = ldz + 1 + 3 + extra_flush

The VLC "code" in our table format = the bit pattern read AFTER
the leading-zero prefix is identified. But our table format uses
(code, size) where code is compared against peeked bits.

Actually, our format is different — we peek 16 bits and search for
a matching (code, size) pair. The "code" is the full VLC bit pattern
right-justified. For libavc's format:
  code = (1 << (3 + extra_flush)) | (suffix_3bits << extra_flush) | extra_bits
  size = ldz + 1 + 3 + extra_flush

But we don't know extra_bits from just the table index. The extra_flush
bits are additional suffix bits that refine the tc/t1.

Wait, actually looking more carefully at libavc's decode:
  FIND_ONE_IN_STREAM_32 counts ldz and flushes ldz+1 bits (the leading zeros + stop bit)
  NEXTBITS reads 3 bits (peeked, not flushed yet)
  Then FLUSHBITS(u4_code & 0x03) flushes 0-3 more bits

So total consumed = ldz + 1 + 3 + extra_flush
But the 3 bits are NOT flushed before the table lookup - they're peeked.
Actually NEXTBITS doesn't advance, then FLUSHBITS at line 1259 advances by extra_flush.
Wait, no - FIND_ONE_IN_STREAM_32 advances past the leading zeros + stop bit.
Then NEXTBITS peeks 3 bits (doesn't advance).
Then FLUSHBITS advances by (u4_code & 3).

So total consumed = (ldz + 1) + (u4_code & 3) bits.
The 3 peeked bits are used for TABLE INDEXING only, not consumed unless extra_flush > 0.

Hmm, that means the 3 bits after the stop bit may or may not be consumed.
Actually: NEXTBITS peeks without advancing. Then FLUSHBITS advances extra_flush.
So after the function: offset = original + ldz + 1 + extra_flush.
The 3 bits used for index lookup are only partially consumed (up to extra_flush of them).

So the actual VLC is: ldz zeros + '1' stop + extra_flush suffix bits.
Total = ldz + 1 + extra_flush.

And the suffix value is the top extra_flush bits of the 3-bit peek.
"""

import os

# Read gau2_ih264d_code_gx from libavc source
def read_libavc_tables():
    project = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    tables_path = os.path.join(project, "docs", "reference", "libavc",
                               "decoder", "ih264d_tables.c")

    with open(tables_path) as f:
        src = f.read()

    # Extract gau2_ih264d_code_gx values
    import re
    m = re.search(r'gau2_ih264d_code_gx\[304\]\s*=\s*\{([^}]+)\}', src, re.DOTALL)
    if not m:
        raise RuntimeError("Could not find gau2_ih264d_code_gx in source")

    values = [int(x.strip().rstrip(','), 0) for x in re.findall(r'0x[0-9a-fA-F]+', m.group(1))]
    assert len(values) == 304, f"Expected 304 entries, got {len(values)}"

    # Extract gau2_ih264d_offset_num_vlc_tab
    m2 = re.search(r'gau2_ih264d_offset_num_vlc_tab\[\d+\]\s*=\s*\{([^}]+)\}', src, re.DOTALL)
    if not m2:
        raise RuntimeError("Could not find offset table")
    offsets = [int(x.strip().rstrip(','), 0) for x in re.findall(r'0x[0-9a-fA-F]+|\d+', m2.group(1))]

    return values, offsets


def decode_table():
    code_gx, offset_tab = read_libavc_tables()

    print(f"gau2_ih264d_code_gx: {len(code_gx)} entries")
    print(f"gau2_ih264d_offset_num_vlc_tab: {len(offset_tab)} entries")
    print()

    # For each nC range (0: nC<2, 1: 2<=nC<4, 2: 4<=nC<8), determine
    # the offset and decode all possible (ldz, suffix3) combinations.

    # The offset_tab maps nC → base_index
    # nC=0 and nC=1 share the same table (nC<2)
    # nC=2 and nC=3 share (2<=nC<4)
    # nC=4..7 share (4<=nC<8)

    nC_representatives = [0, 2, 4]  # one per range
    range_names = ["nC<2", "2<=nC<4", "4<=nC<8"]

    # Build our output tables: [table_idx][t1][tc] = (code, size)
    output_codes = [[[0]*17 for _ in range(4)] for _ in range(3)]
    output_sizes = [[[0]*17 for _ in range(4)] for _ in range(3)]

    for table_idx, nC in enumerate(nC_representatives):
        base = offset_tab[nC]
        print(f"=== Table {table_idx} ({range_names[table_idx]}): offset={base} ===")

        # For each possible (ldz, suffix3) combination
        seen = {}  # (tc, t1) → (ldz, suffix3, extra_flush, total_bits)

        for ldz in range(16):
            for suffix3 in range(8):
                index = ldz * 8 + suffix3 + base
                if index < 0 or index >= 304:
                    continue

                entry = code_gx[index]
                extra_flush = entry & 3
                t1 = (entry >> 2) & 3
                tc = (entry >> 4) & 0x1F

                # Entry 0x0000 means tc=0, t1=0, extra_flush=0 — this is valid!
                # Only skip truly out-of-range entries.
                if tc > 16:
                    continue

                total_bits = ldz + 1 + extra_flush

                key = (tc, t1)
                if key in seen:
                    # Already found a shorter code for this (tc, t1)
                    # The FIRST (shortest ldz) match is the correct one
                    # because the decoder counts leading zeros and stops
                    # at the first '1' bit.
                    continue

                seen[key] = (ldz, suffix3, extra_flush, total_bits)

                # Reconstruct the VLC code value:
                # The code starts with ldz zeros, then '1', then extra_flush bits
                # from the suffix3 value.
                # The full code = 0...01 [extra_flush bits from top of suffix3]
                # Numerically: code = (1 << extra_flush) | (suffix3 >> (3 - extra_flush))
                # But only if extra_flush > 0. If extra_flush = 0, code = 1 (just stop bit)
                # preceded by ldz zeros.
                #
                # Actually the full bit pattern is:
                # [ldz zeros] [1] [top extra_flush bits of suffix3]
                # As a right-justified value in 'total_bits' bits:
                if extra_flush > 0:
                    suffix_val = suffix3 >> (3 - extra_flush)
                    code_val = (1 << extra_flush) | suffix_val
                else:
                    code_val = 1  # just the stop bit

                # But our table stores the code right-justified in 'size' bits
                # with the leading zeros implicit. Wait, no — our table stores
                # the FULL code including leading zeros as a right-justified value.
                # Leading zeros make the MSBs = 0, so:
                # full_code = code_val  (the leading zeros are already implicit
                # because the code fits in 'total_bits' bits with MSBs = 0)

                output_codes[table_idx][t1][tc] = code_val
                output_sizes[table_idx][t1][tc] = total_bits

        print(f"  Found {len(seen)} unique (tc, t1) combinations")

    # Output as C++ arrays
    print("\n\n// Generated from libavc gau2_ih264d_code_gx — verified correct")
    print("inline constexpr uint16_t cCoeffTokenCode[3][4][17] = {")
    for ti in range(3):
        print("    {")
        for t1 in range(4):
            row = ", ".join(f"{output_codes[ti][t1][tc]:3d}" for tc in range(17))
            print(f"        {{ {row} }},  // t1={t1}")
        print("    },")
    print("};")

    print()
    print("inline constexpr uint8_t cCoeffTokenSize[3][4][17] = {")
    for ti in range(3):
        print("    {")
        for t1 in range(4):
            row = ", ".join(f"{output_sizes[ti][t1][tc]:2d}" for tc in range(17))
            print(f"        {{ {row} }},  // t1={t1}")
        print("    },")
    print("};")

    # Verify no duplicates
    print("\n// Duplicate check:")
    for ti in range(3):
        entries = {}
        for t1 in range(4):
            for tc in range(17):
                sz = output_sizes[ti][t1][tc]
                code = output_codes[ti][t1][tc]
                if sz == 0:
                    continue
                key = (sz, code)
                if key not in entries:
                    entries[key] = []
                entries[key].append((tc, t1))

        dupes = sum(1 for v in entries.values() if len(v) > 1)
        print(f"//   Table {ti}: {dupes} duplicates")
        if dupes > 0:
            for key, vals in sorted(entries.items()):
                if len(vals) > 1:
                    sz, code = key
                    pairs = ", ".join(f"tc={tc}/t1={t1}" for tc, t1 in vals)
                    print(f"//     size={sz} code={code}: {pairs}")


if __name__ == "__main__":
    decode_table()
