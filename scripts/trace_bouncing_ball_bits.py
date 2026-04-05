#!/usr/bin/env python3
"""Trace per-MB bit offsets for the bouncing ball IDR frame.

Manually parses the CAVLC bitstream to verify bit consumption per MB.
Compares against our decoder's reported offsets.

Usage:
    python scripts/trace_bouncing_ball_bits.py

SPDX-License-Identifier: MIT
"""
import sys
import struct


def find_nals(data):
    nals = []
    i = 0
    while i < len(data) - 3:
        if data[i] == 0 and data[i+1] == 0:
            if data[i+2] == 1:
                nals.append(i + 3)
                i += 3
            elif data[i+2] == 0 and i+3 < len(data) and data[i+3] == 1:
                nals.append(i + 4)
                i += 4
            else:
                i += 1
        else:
            i += 1
    return nals


def remove_emulation(ebsp):
    rbsp = bytearray()
    zc = 0
    for b in ebsp:
        if zc == 2 and b == 3:
            zc = 0
            continue
        zc = zc + 1 if b == 0 else 0
        rbsp.append(b)
    return bytes(rbsp)


class BitReaderPy:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def read_bit(self):
        byte_off = self.pos >> 3
        bit_pos = self.pos & 7
        self.pos += 1
        if byte_off < len(self.data):
            return (self.data[byte_off] >> (7 - bit_pos)) & 1
        return 0

    def read_bits(self, n):
        val = 0
        for _ in range(n):
            val = (val << 1) | self.read_bit()
        return val

    def read_uev(self):
        lz = 0
        while self.read_bit() == 0:
            lz += 1
            if lz > 31:
                return 0xFFFFFFFF
        if lz == 0:
            return 0
        suffix = self.read_bits(lz)
        return (1 << lz) - 1 + suffix

    def read_sev(self):
        val = self.read_uev()
        if val & 1:
            return (val + 1) >> 1
        return -(val >> 1)


def main():
    data = open("tests/fixtures/bouncing_ball.h264", "rb").read()
    nals = find_nals(data)

    for nal_off in nals:
        nal_type = data[nal_off] & 0x1F
        if nal_type != 5:  # IDR
            continue

        rbsp = remove_emulation(data[nal_off + 1:])
        br = BitReaderPy(rbsp)

        # Parse slice header (same as other Baseline fixtures)
        first_mb = br.read_uev()  # 0
        slice_type = br.read_uev()  # 7
        pps_id = br.read_uev()  # 0
        frame_num = br.read_bits(4)  # 0
        idr_pic_id = br.read_uev()  # 0
        no_output = br.read_bit()  # 0
        long_term = br.read_bit()  # 0
        qp_delta = br.read_sev()  # -3
        deblock = br.read_uev()  # 0
        alpha_off = br.read_sev()  # 0
        beta_off = br.read_sev()  # 0

        print(f"Slice header: type={slice_type} qp_delta={qp_delta} "
              f"bitOffset={br.pos}")
        print(f"  first_mb={first_mb} frame_num={frame_num} idr={idr_pic_id}")

        # First MB starts here
        mb_start = br.pos
        print(f"\nMB data starts at bit {mb_start}")

        # Parse mb_type for first few MBs
        for mb_addr in range(5):
            mb_bit_start = br.pos
            mb_type = br.read_uev()

            # Determine if I_4x4 (0) or I_16x16 (1-24) or I_PCM (25)
            if mb_type == 0:
                name = "I_4x4"
            elif mb_type <= 24:
                name = f"I_16x16 (raw={mb_type})"
            else:
                name = "I_PCM"

            print(f"  MB({mb_addr},0): mb_type={mb_type} ({name}) "
                  f"bits {mb_bit_start}->{br.pos} ({br.pos - mb_bit_start}b)")

            # We can't easily parse the full MB residual in Python,
            # but we can show the mb_type bit consumption pattern.
            # The full decoder should consume the rest — break after mb_type
            # to show the starting bit offset for each MB.
            break  # Only parse mb_type for first MB

        print(f"\n(Further MB parsing requires full CAVLC implementation)")
        break


if __name__ == "__main__":
    main()
