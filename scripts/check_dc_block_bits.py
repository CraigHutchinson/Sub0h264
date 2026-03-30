#!/usr/bin/env python3
"""Check DC block bits at the start of MB 0 for baseline stream.

Reads the IDR RBSP and examines bits at offset 35 (where the DC
Hadamard block should start for the first I_16x16 MB).

SPDX-License-Identifier: MIT
"""
import sys


def main():
    with open("tests/fixtures/baseline_640x480_short.h264", "rb") as f:
        data = f.read()

    # Find all NALs first
    nals = []
    for i in range(len(data) - 4):
        if data[i:i+4] == b'\x00\x00\x00\x01':
            nal_type = data[i+4] & 0x1F
            nals.append((i+4, nal_type))
        elif i > 0 and data[i-1:i+3] != b'\x00\x00\x00\x01' and data[i:i+3] == b'\x00\x00\x01':
            nal_type = data[i+3] & 0x1F
            nals.append((i+3, nal_type))
    print(f"Found {len(nals)} NALs: types={[t for _, t in nals[:10]]}")

    # Find IDR NAL (type 5)
    for nal_off, nal_type in nals:
        if nal_type == 5:
            i = nal_off - 4  # back to start code
            print(f"IDR NAL at offset {nal_off}")
            # Use nal_off+1 as RBSP start (skip NAL header byte)
            # Remove emulation prevention — RBSP starts after NAL header byte
            raw = data[nal_off+1:nal_off+200]
            rbsp = bytearray()
            j = 0
            while j < len(raw):
                if j+2 < len(raw) and raw[j] == 0 and raw[j+1] == 0 and raw[j+2] == 3:
                    rbsp.extend([0, 0])
                    j += 3
                else:
                    rbsp.append(raw[j])
                    j += 1

            def bit(off):
                return (rbsp[off // 8] >> (7 - off % 8)) & 1

            def read_ue(pos):
                lz = 0
                while bit(pos + lz) == 0:
                    lz += 1
                val = 0
                for k in range(lz):
                    val = (val << 1) | bit(pos + 1 + lz + k)
                return (1 << lz) - 1 + val, 2 * lz + 1

            # Show bits 35-70 (DC block region for MB 0)
            bits_str = "".join(str(bit(k)) for k in range(35, 71))
            print(f"Bits 35-70: {bits_str}")
            print(f"Bit 35 = {bit(35)}")

            # nC=0 coeff_token lookup:
            #   TC=0,TO=0: code=1 (1 bit) → single '1'
            #   TC=1,TO=1: code=01 (2 bits)
            #   TC=2,TO=2: code=001 (3 bits)
            #   TC=1,TO=0: code=000101 (6 bits)
            if bit(35) == 1:
                print("  → coeff_token: TC=0,TO=0 (no coefficients) — 1 bit")
                print("  But C++ decoder reports TC=1! Mismatch.")
            elif bit(36) == 1:
                print("  → coeff_token: TC=1,TO=1 — 2 bits")
                print("  Sign bit at 37:", bit(37), "→ level =", -1 if bit(37) else 1)
                # total_zeros
                print("  total_zeros VLC starts at bit 38")
            elif bit(37) == 1:
                print("  → coeff_token: TC=2,TO=2 — 3 bits")
            else:
                # Longer code — show first 16 bits
                first16 = "".join(str(bit(35 + k)) for k in range(16))
                print(f"  → Longer code. First 16 bits from 35: {first16}")

            # Also check: what does the C++ decoder's nC=0 table match?
            # Peek 16 bits from offset 35
            peek16 = 0
            for k in range(16):
                peek16 = (peek16 << 1) | bit(35 + k)
            print(f"\n  peek16 from bit 35: 0x{peek16:04x} = {peek16:016b}")

            # Check our VLC table entries for nC<2:
            # TC=0,TO=0: code=1 size=1 → peek>>15 should be 1
            # TC=1,TO=1: code=1 size=2 → peek>>14 should be 01
            top1 = (peek16 >> 15) & 1
            top2 = (peek16 >> 14) & 3
            top6 = (peek16 >> 10) & 0x3F
            print(f"  top1={top1} (TC=0,TO=0 needs 1)")
            print(f"  top2={top2} (TC=1,TO=1 needs 1 → binary 01)")
            print(f"  top6={top6} (TC=1,TO=0 needs 5 → binary 000101)")

            break

    print("\nDone.")


if __name__ == "__main__":
    main()
