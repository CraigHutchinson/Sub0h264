#!/usr/bin/env python3
"""Dump the IDR RBSP bytes of cabac_flat_main.h264, with and without EPB removal."""
import sys

data = open("tests/fixtures/cabac_flat_main.h264", "rb").read()

# Find IDR NAL
nals = []
i = 0
while i < len(data) - 3:
    if data[i] == 0 and data[i+1] == 0:
        if data[i+2] == 1:
            nals.append(i + 3)
            i += 3
        elif data[i+2] == 0 and i + 3 < len(data) and data[i+3] == 1:
            nals.append(i + 4)
            i += 4
        else:
            i += 1
    else:
        i += 1

for nal_off in nals:
    nal_type = data[nal_off] & 0x1F
    if nal_type == 5:
        print(f"IDR NAL at byte offset {nal_off}")
        print(f"NAL header: 0x{data[nal_off]:02x}")

        # Raw bytes after NAL header (EBSP)
        ebsp = data[nal_off + 1:]
        print(f"\nEBSP (first 80 bytes):")
        for row in range(0, min(80, len(ebsp)), 16):
            hex_str = ' '.join(f'{b:02x}' for b in ebsp[row:row+16])
            print(f"  {row:3d}: {hex_str}")

        # Remove EPB
        rbsp = bytearray()
        j = 0
        epb_positions = []
        while j < len(ebsp):
            if j >= 2 and ebsp[j-2] == 0 and ebsp[j-1] == 0 and ebsp[j] == 3:
                epb_positions.append(j)
                j += 1
                continue
            rbsp.append(ebsp[j])
            j += 1

        print(f"\nEPB removed at positions: {epb_positions}")
        print(f"EBSP size: {len(ebsp)}, RBSP size: {len(rbsp)}")

        print(f"\nRBSP (first 80 bytes):")
        for row in range(0, min(80, len(rbsp)), 16):
            hex_str = ' '.join(f'{b:02x}' for b in rbsp[row:row+16])
            print(f"  {row:3d}: {hex_str}")

        # Slice header is 24 bits = 3 bytes
        print(f"\nSlice header (3 bytes): {' '.join(f'{b:02x}' for b in rbsp[:3])}")
        print(f"CABAC data (from byte 3): {' '.join(f'{b:02x}' for b in rbsp[3:13])}")

        # CABAC init: first 9 bits of CABAC data
        cabac_byte0 = rbsp[3]
        cabac_byte1 = rbsp[4]
        cabac_init = (cabac_byte0 << 1) | (cabac_byte1 >> 7)
        print(f"\nCABAC init 9 bits: {cabac_init} (0x{cabac_init:03x})")
        print(f"Expected: 509 (0x1FD)")

        # Total RBSP size
        print(f"\nTotal RBSP: {len(rbsp)} bytes = {len(rbsp)*8} bits")
        print(f"CABAC data: {len(rbsp)-3} bytes = {(len(rbsp)-3)*8} bits")
        break
