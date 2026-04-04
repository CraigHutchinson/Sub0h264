#!/usr/bin/env python3
"""Check for emulation prevention bytes in an H.264 NAL unit.

Finds all 00 00 03 sequences in the IDR NAL and verifies that the RBSP
extraction handles them correctly.

Usage:
    python scripts/check_emulation_bytes.py <h264_file>

SPDX-License-Identifier: MIT
"""
import argparse
import sys


def find_nal_units(data):
    nals = []
    i = 0
    while i < len(data) - 3:
        if data[i] == 0 and data[i + 1] == 0:
            if data[i + 2] == 1:
                nals.append(("3byte", i + 3))
                i += 3
                continue
            elif data[i + 2] == 0 and i + 3 < len(data) and data[i + 3] == 1:
                nals.append(("4byte", i + 4))
                i += 4
                continue
        i += 1
    return nals


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("h264_file")
    args = parser.parse_args()

    data = open(args.h264_file, "rb").read()
    nals = find_nal_units(data)

    for idx, (sc_type, offset) in enumerate(nals):
        nal_type = data[offset] & 0x1F
        if nal_type not in (1, 5):
            continue

        # Find NAL end
        nal_end = len(data)
        for nidx in range(idx + 1, len(nals)):
            nal_end = nals[nidx][1] - (4 if nals[nidx][0] == "4byte" else 3)
            break

        nal_body = data[offset + 1:nal_end]
        name = "IDR" if nal_type == 5 else "P-slice"
        print(f"NAL {idx}: {name} at offset {offset}, body size {len(nal_body)} bytes")

        # Check for emulation prevention bytes
        emul_count = 0
        for i in range(len(nal_body) - 2):
            if nal_body[i] == 0 and nal_body[i + 1] == 0 and nal_body[i + 2] == 3:
                print(f"  Emulation prevention byte at body offset {i}: "
                      f"00 00 03 {nal_body[i+3]:02x}")
                emul_count += 1

        if emul_count == 0:
            print("  No emulation prevention bytes found")

        # Show first 20 raw body bytes (before emulation removal)
        print(f"  Raw body (first 20): {' '.join(f'{b:02x}' for b in nal_body[:20])}")

        # Manual RBSP extraction
        rbsp = bytearray()
        zero_count = 0
        for b in nal_body:
            if zero_count == 2 and b == 3:
                zero_count = 0
                continue
            if b == 0:
                zero_count += 1
            else:
                zero_count = 0
            rbsp.append(b)

        print(f"  RBSP (first 20): {' '.join(f'{b:02x}' for b in rbsp[:20])}")
        print(f"  Raw size: {len(nal_body)}, RBSP size: {len(rbsp)}, "
              f"removed: {len(nal_body) - len(rbsp)}")


if __name__ == "__main__":
    main()
