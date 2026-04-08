#!/usr/bin/env python3
"""Check CABAC init offset at various slice header bit positions."""
import sys

data = open("tests/fixtures/cabac_min_u128.h264", "rb").read()
for i in range(len(data)-3):
    if data[i]==0 and data[i+1]==0:
        if data[i+2]==1:
            off = i+3
        elif i+3 < len(data) and data[i+2]==0 and data[i+3]==1:
            off = i+4
        else:
            continue
        if (data[off] & 0x1f) == 5:
            rbsp = data[off+1:]
            print("RBSP bytes:", " ".join("%02x" % b for b in rbsp))
            bits = []
            for b in rbsp:
                for bi in range(7, -1, -1):
                    bits.append((b >> bi) & 1)

            # Try CABAC init at different bit positions
            for start in range(20, 30):
                val = 0
                for j in range(9):
                    if start + j < len(bits):
                        val = (val << 1) | bits[start + j]
                print("  Init at bit %d: %d (0x%03x)" % (start, val, val))
            break
