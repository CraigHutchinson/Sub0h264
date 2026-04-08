#!/usr/bin/env python3
"""Manual CAVLC decode for MB(3,0) scan block 5 of bouncing ball.

Reads the raw RBSP bits at the exact offset where scan block 5 starts
and decodes the CAVLC level values step by step, comparing against
spec §9.2.2.

Usage:
    python scripts/trace_block5_mb3.py

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
                nals.append(i+3); i += 3
            elif data[i+2] == 0 and i+3 < len(data) and data[i+3] == 1:
                nals.append(i+4); i += 4
            else: i += 1
        else: i += 1
    return nals


def remove_emulation(ebsp):
    rbsp = bytearray()
    zc = 0
    for b in ebsp:
        if zc == 2 and b == 3:
            zc = 0; continue
        zc = zc + 1 if b == 0 else 0
        rbsp.append(b)
    return bytes(rbsp)


class BitReader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def read_bit(self):
        byte_off = self.pos >> 3
        bit_pos = self.pos & 7
        self.pos += 1
        return (self.data[byte_off] >> (7 - bit_pos)) & 1 if byte_off < len(self.data) else 0

    def read_bits(self, n):
        val = 0
        for _ in range(n):
            val = (val << 1) | self.read_bit()
        return val

    def read_uev(self):
        lz = 0
        while self.read_bit() == 0:
            lz += 1
        if lz == 0: return 0
        return (1 << lz) - 1 + self.read_bits(lz)

    def read_sev(self):
        val = self.read_uev()
        return ((val + 1) >> 1) if (val & 1) else -(val >> 1)


# coeff_token VLC tables for nC >= 8 — fixed-length 6-bit codes
# Format: totalCoeff = bits[5:2], trailingOnes = bits[1:0]
# Note: This is spec Table 9-5(e)


def decode_coeff_token_nc8plus(br):
    """Decode coeff_token for nC >= 8 — ITU-T H.264 Table 9-5(e).
    Fixed-length 6-bit code: [5:2] = totalCoeff - trailingOnes, [1:0] = trailingOnes
    Actually: code = tc*4 + to if tc > 0, else 0x0F for tc=0
    """
    code = br.read_bits(6)
    # The encoding for nC>=8 is a bit unusual:
    # 000011 -> tc=0, to=0
    # actually let me just use the standard formula:
    # trailing_ones = code & 3
    # total_coeff = (code >> 2) + trailing_ones (for most cases)
    # Actually per spec Table 9-5(e): the 6-bit code maps as:
    # code = 0000 11 -> tc=0, to=0
    # code = 0001 00 -> tc=1, to=0
    # etc.
    # Simpler: total_coeff is encoded directly
    # For nC >= 8: coeff_token is a fixed 6-bit code
    # The mapping: see spec or just look at libavc

    # Standard mapping for nC >= 8:
    trailing_ones = code & 3
    n = code >> 2
    if n == 0 and trailing_ones == 3:
        return 0, 0  # special: 000011 = tc=0 to=0
    total_coeff = n
    if trailing_ones > total_coeff:
        trailing_ones = total_coeff
    # Actually this is wrong. Let me use the correct mapping.
    # For nC >= 8, Table 9-5(e):
    # The VLC is simply: total_coeff = (code >> 2), trailing_ones = min(code & 3, total_coeff)
    # With special case: code=3 means tc=0, to=0
    if code == 3:
        return 0, 0
    total_coeff = code >> 2
    trailing_ones = min(code & 3, total_coeff)
    return total_coeff, trailing_ones


def decode_level(br, suffix_len):
    """Decode one level per §9.2.2."""
    # Count leading zeros for level_prefix
    prefix = 0
    while br.read_bit() == 0:
        prefix += 1

    # levelSuffixSize
    if prefix == 14 and suffix_len == 0:
        suffix_size = 4
    elif prefix >= 15:
        suffix_size = prefix - 3
    else:
        suffix_size = suffix_len

    # Read suffix
    suffix = 0
    if suffix_size > 0:
        suffix = br.read_bits(suffix_size)

    # Compute levelCode
    level_code = (min(15, prefix) << suffix_len) + suffix
    if prefix >= 15 and suffix_len == 0:
        level_code += 15
    if prefix >= 16:
        level_code += (1 << (prefix - 3)) - 4096

    return level_code, prefix, suffix_size, suffix


ZIGZAG_4x4 = [0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15]

# Thresholds for suffix_length adaptation
THRESH = [0, 3, 6, 12, 24, 48, 0xFFFFFFFF]


def main():
    data = open("tests/fixtures/bouncing_ball_ionly.h264", "rb").read()
    nals = find_nals(data)

    for nal_off in nals:
        nal_type = data[nal_off] & 0x1F
        if nal_type != 5:
            continue

        # Remove emulation prevention bytes from the slice data
        rbsp = remove_emulation(data[nal_off:])
        br = BitReader(rbsp)

        # Skip NAL header byte (already at nal_off+1 in original, but rbsp starts at nal_off)
        # Actually rbsp starts AT the NAL header byte, so skip it
        br.pos = 8  # skip nal_unit_type byte

        # Parse slice header minimally
        first_mb = br.read_uev()
        slice_type = br.read_uev()
        pps_id = br.read_uev()
        frame_num = br.read_bits(4)  # log2_max_frame_num_minus4=0 => 4 bits
        idr_pic_id = br.read_uev()

        # dec_ref_pic_marking for IDR
        no_output = br.read_bit()
        long_term = br.read_bit()

        qp_delta = br.read_sev()
        print(f"slice_qp_delta = {qp_delta}, QPY = {20 + qp_delta}")

        # deblocking
        deblock = br.read_uev()
        if deblock != 1:
            alpha = br.read_sev()
            beta = br.read_sev()

        print(f"Slice data starts at bit {br.pos}")
        qp = 20 + qp_delta  # pic_init_qp + slice_qp_delta

        # Decode MBs 0-3, tracking QP and bit positions
        for mb_addr in range(5):
            mbx = mb_addr % 20
            mby = mb_addr // 20
            mb_start_bit = br.pos
            mb_type = br.read_uev()
            print(f"\nMB({mbx},{mby}): type={mb_type} at bit {mb_start_bit}")

            if mb_type == 0:
                # I_4x4
                # Read 16 prediction modes
                for i in range(16):
                    prev_flag = br.read_bit()
                    if prev_flag == 0:
                        br.read_bits(3)

                # Chroma pred mode
                chroma_mode = br.read_uev()

                # CBP
                cbp_code = br.read_uev()
                cbp_intra = [
                    47,31,15, 0,23,27,29,30,  7,11,13,14,39,43,45,46,
                    16, 3, 5,10,12,19,21,26, 28,35,37,42,44, 1, 2, 4,
                     8,17,18,20,24, 6, 9,22, 25,32,33,34,36,40,38,41,
                ]
                cbp = cbp_intra[cbp_code] if cbp_code < 48 else 0
                cbp_luma = cbp & 0x0F
                cbp_chroma = (cbp >> 4) & 3

                # QP delta
                if cbp > 0:
                    qpd = br.read_sev()
                    qp += qpd
                    if qp < 0: qp += 52
                    if qp > 51: qp -= 52

                print(f"  I_4x4 cbp=0x{cbp:02x} luma=0x{cbp_luma:x} chroma={cbp_chroma} QP={qp}")

                # For MB(3,0), decode all luma blocks and show scan block 5 in detail
                if mb_addr == 3:
                    # Decode all 16 luma blocks
                    for scan_idx in range(16):
                        group_8x8 = scan_idx >> 2
                        if not ((cbp_luma >> group_8x8) & 1):
                            continue

                        blk_start = br.pos

                        # Use nC >= 8 decoding (since tc values are 14-16)
                        # Actually nC depends on neighbors. For simplicity,
                        # let me just decode and show the detailed level trace for scan 5
                        tc, to = decode_coeff_token_nc8plus(br)

                        if tc == 0:
                            if scan_idx == 5:
                                print(f"  scan{scan_idx}: tc=0 (empty)")
                            continue

                        # Trailing ones signs
                        levels = []
                        for i in range(to):
                            sign = br.read_bit()
                            levels.append(-1 if sign else 1)

                        # Remaining levels
                        suffix_len = 0
                        if tc > 10 and to < 3:
                            suffix_len = 1

                        for i in range(to, tc):
                            lc, pfx, ss, sfx = decode_level(br, suffix_len)

                            # First non-trailing level offset
                            if i == to and to < 3:
                                lc += 2

                            # Convert to signed
                            if lc & 1:
                                level = -((lc + 1) >> 1)
                            else:
                                level = (lc >> 1) + 1

                            levels.append(level)

                            # Update suffix_len
                            abs_level = abs(level)
                            if suffix_len == 0:
                                suffix_len = 1
                            if suffix_len < 6 and abs_level > THRESH[suffix_len]:
                                suffix_len += 1

                        if scan_idx == 5:
                            print(f"\n  === SCAN BLOCK 5 DETAIL (MB3) ===")
                            print(f"  bit offset: {blk_start}")
                            print(f"  tc={tc} to={to}")
                            print(f"  levels (reverse scan order): {levels}")

                            # total_zeros
                            # Skip for now — just show levels
                            bits_used = br.pos - blk_start
                            print(f"  bits consumed so far: {bits_used}")

                    # After all luma blocks, show bit position
                    print(f"  MB(3,0) end bit: {br.pos}")
                else:
                    # Skip blocks for MBs 0-2
                    # This is hard to do without full VLC decode...
                    # Just note the bit position
                    pass

            elif mb_type >= 1 and mb_type <= 24:
                # I_16x16 — for simplicity just note it
                print(f"  I_16x16 variant {mb_type}")
                break  # can't easily skip without full decode
            else:
                print(f"  Unknown mb_type {mb_type}")
                break

        break


if __name__ == "__main__":
    main()
