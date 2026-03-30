#!/usr/bin/env python3
"""Verify per-MB bit consumption against C++ decoder trace.

Implements a minimal CAVLC decoder in Python to independently verify
bit consumption for specific MBs. Compares against our C++ decoder's
trace output to find the first bitstream read discrepancy.

Usage:
    python scripts/verify_mb_decode.py tests/fixtures/baseline_640x480_short.h264

SPDX-License-Identifier: MIT
"""
import sys

# Coeff token VLC tables — must match our cavlc_tables.hpp exactly.
# [nC_range][trailingOnes][totalCoeff] → (code, size)
# nC_range: 0=nC<2, 1=2<=nC<4, 2=4<=nC<8
COEFF_TOKEN_CODE = [
    # nC < 2
    [
        [1, 5, 7, 7, 7, 7, 15, 11, 8, 15, 11, 15, 11, 15, 11, 7, 4],
        [0, 1, 4, 6, 6, 6, 6, 14, 10, 14, 10, 14, 10, 1, 14, 10, 6],
        [0, 0, 1, 5, 5, 5, 5, 5, 13, 9, 13, 9, 13, 9, 13, 9, 5],
        [0, 0, 0, 3, 3, 4, 4, 4, 4, 4, 12, 12, 8, 12, 8, 12, 8],
    ],
    # 2 <= nC < 4
    [
        [3, 11, 7, 7, 7, 4, 7, 15, 11, 15, 11, 8, 15, 11, 7, 9, 7],
        [0, 2, 7, 10, 6, 6, 6, 6, 14, 10, 14, 10, 14, 10, 11, 8, 6],
        [0, 0, 3, 9, 5, 5, 5, 5, 13, 9, 13, 9, 13, 9, 6, 10, 5],
        [0, 0, 0, 5, 4, 6, 8, 4, 4, 4, 12, 8, 12, 12, 8, 1, 4],
    ],
    # 4 <= nC < 8
    [
        [15, 15, 11, 8, 15, 11, 9, 8, 15, 11, 15, 11, 8, 13, 9, 5, 1],
        [0, 14, 15, 12, 10, 8, 14, 10, 14, 14, 10, 14, 10, 7, 12, 8, 4],
        [0, 0, 13, 14, 11, 9, 13, 9, 13, 10, 13, 9, 13, 9, 11, 7, 3],
        [0, 0, 0, 12, 11, 10, 9, 8, 13, 12, 12, 12, 8, 12, 10, 6, 2],
    ],
]

COEFF_TOKEN_SIZE = [
    [
        [1, 6, 8, 9, 10, 11, 13, 13, 13, 14, 14, 15, 15, 16, 16, 16, 16],
        [0, 2, 6, 8, 9, 10, 11, 13, 13, 14, 14, 15, 15, 15, 16, 16, 16],
        [0, 0, 3, 7, 8, 9, 10, 11, 13, 13, 14, 14, 15, 15, 16, 16, 16],
        [0, 0, 0, 5, 6, 7, 8, 9, 10, 11, 13, 14, 14, 15, 15, 16, 16],
    ],
    [
        [2, 6, 6, 7, 8, 8, 9, 11, 11, 12, 12, 12, 13, 13, 13, 14, 14],
        [0, 2, 5, 6, 6, 7, 8, 9, 11, 11, 12, 12, 13, 13, 14, 14, 14],
        [0, 0, 3, 6, 6, 7, 8, 9, 11, 11, 12, 12, 13, 13, 13, 14, 14],
        [0, 0, 0, 4, 4, 5, 6, 6, 7, 9, 11, 11, 12, 13, 13, 13, 14],
    ],
    [
        [4, 6, 6, 6, 7, 7, 7, 7, 8, 8, 9, 9, 9, 10, 10, 10, 10],
        [0, 4, 5, 5, 5, 5, 6, 6, 7, 8, 8, 9, 9, 9, 10, 10, 10],
        [0, 0, 4, 5, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 10],
        [0, 0, 0, 4, 4, 4, 4, 4, 5, 6, 7, 8, 8, 9, 10, 10, 10],
    ],
]

# Chroma DC coeff_token
CHROMA_CODE = [
    [1, 7, 4, 3, 2],
    [0, 1, 6, 3, 3],
    [0, 0, 1, 2, 2],
    [0, 0, 0, 5, 0],
]
CHROMA_SIZE = [
    [2, 6, 6, 6, 6],
    [0, 1, 6, 7, 8],
    [0, 0, 3, 7, 8],
    [0, 0, 0, 6, 7],
]

# CBP intra table
CBP_INTRA = [
    47, 31, 15, 0, 23, 27, 29, 30, 7, 11, 13, 14,
    39, 43, 45, 46, 16, 3, 5, 10, 12, 19, 21, 26,
    28, 35, 37, 42, 44, 1, 2, 4, 8, 17, 18, 20,
    24, 6, 9, 22, 25, 32, 33, 34, 36, 40, 38, 41,
]

# Total zeros index offsets
TZ_INDEX = [0, 16, 31, 45, 58, 70, 81, 91, 100, 108, 115, 121, 126, 130, 133]
TZ_SIZE = [
    1, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 9,
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
    1, 1,
]
TZ_CODE = [
    1, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 1,
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
    0, 1,
]

# Run before
RB_INDEX = [0, 2, 5, 9, 14, 20, 27]
RB_SIZE = [
    1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3,
    2, 2, 3, 3, 3, 3, 2, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 4, 5, 6, 7, 8, 9, 10, 11,
]
RB_CODE = [
    1, 0, 1, 1, 0, 3, 2, 1, 0, 3, 2, 1, 1, 0,
    3, 2, 3, 2, 1, 0, 3, 0, 1, 3, 2, 5, 4,
    7, 6, 5, 4, 3, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
]

# Zigzag scan
ZIGZAG_4x4 = [0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15]


class BitReader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def bit(self):
        b = (self.data[self.pos // 8] >> (7 - self.pos % 8)) & 1
        self.pos += 1
        return b

    def peek(self, n):
        val = 0
        for i in range(n):
            off = self.pos + i
            val = (val << 1) | ((self.data[off // 8] >> (7 - off % 8)) & 1)
        return val

    def skip(self, n):
        self.pos += n

    def read_bits(self, n):
        val = 0
        for _ in range(n):
            val = (val << 1) | self.bit()
        return val

    def read_ue(self):
        lz = 0
        while self.bit() == 0:
            lz += 1
        val = 0
        for _ in range(lz):
            val = (val << 1) | self.bit()
        return (1 << lz) - 1 + val

    def read_se(self):
        ue = self.read_ue()
        return (ue + 1) // 2 if ue % 2 else -(ue // 2)


def decode_coeff_token(br, nc):
    """Decode coeff_token VLC, return (totalCoeff, trailingOnes, bits_consumed)."""
    start = br.pos
    if nc < 0:
        # Chroma DC
        peek = br.peek(8)
        best_tc, best_to, best_size = 0, 0, 255
        for to in range(4):
            for tc in range(5):
                if to > tc:
                    continue
                sz = CHROMA_SIZE[to][tc]
                if sz == 0:
                    continue
                code = CHROMA_CODE[to][tc]
                peeked = (peek >> (8 - sz)) & ((1 << sz) - 1)
                if peeked == code and sz < best_size:
                    best_tc, best_to, best_size = tc, to, sz
        br.skip(best_size)
        return best_tc, best_to, br.pos - start

    if nc >= 8:
        code = br.read_bits(6)
        to = code & 3
        tc = code >> 2
        return tc, to, 6

    table_idx = 0 if nc < 2 else (1 if nc < 4 else 2)
    peek = br.peek(16)
    best_tc, best_to, best_size = 0, 0, 255
    for to in range(4):
        for tc in range(17):
            if to > tc:
                continue
            sz = COEFF_TOKEN_SIZE[table_idx][to][tc]
            if sz == 0 or sz > 16:
                continue
            code = COEFF_TOKEN_CODE[table_idx][to][tc]
            peeked = (peek >> (16 - sz)) & ((1 << sz) - 1)
            if peeked == code and sz < best_size:
                best_tc, best_to, best_size = tc, to, sz
    br.skip(best_size)
    return best_tc, best_to, br.pos - start


def decode_level(br, suffix_len):
    """Decode one coefficient level."""
    start = br.pos
    prefix = 0
    while br.bit() == 0:
        prefix += 1
    level_code = min(prefix, 15)

    if prefix == 14 and suffix_len == 0:
        suffix = br.read_bits(4)
        level_code = (level_code << 4) | suffix
    elif prefix >= 15:
        suffix = br.read_bits(prefix - 3)
        level_code = (level_code << (prefix - 3)) | suffix

    if suffix_len > 0 and prefix < 15:
        suffix = br.read_bits(suffix_len)
        level_code = (level_code << suffix_len) | suffix

    level_code += 2  # offset for non-trailing-ones
    level = (level_code + 2) // 2 if level_code % 2 == 0 else -(level_code + 1) // 2
    return level, br.pos - start


def decode_total_zeros(br, total_coeff):
    """Decode total_zeros VLC."""
    if total_coeff == 0 or total_coeff >= 16:
        return 0, 0
    start = br.pos
    offset = TZ_INDEX[total_coeff - 1]
    n_entries = (TZ_INDEX[total_coeff] if total_coeff < 15 else 135) - offset
    peek = br.peek(9)
    for tz_val in range(n_entries):
        sz = TZ_SIZE[offset + tz_val]
        code = TZ_CODE[offset + tz_val]
        if sz == 0 or sz > 9:
            continue
        peeked = (peek >> (9 - sz)) & ((1 << sz) - 1)
        if peeked == code:
            br.skip(sz)
            return tz_val, br.pos - start
    br.skip(1)
    return 0, br.pos - start


def decode_run_before(br, zeros_left):
    """Decode run_before VLC."""
    if zeros_left == 0:
        return 0, 0
    start = br.pos
    if zeros_left <= 6:
        offset = RB_INDEX[zeros_left - 1]
        n_entries = (RB_INDEX[zeros_left] if zeros_left < 6 else 27) - offset
        peek = br.peek(3)
        for rv in range(n_entries):
            sz = RB_SIZE[offset + rv]
            code = RB_CODE[offset + rv]
            peeked = (peek >> (3 - sz)) & ((1 << sz) - 1)
            if peeked == code:
                br.skip(sz)
                return rv, br.pos - start
        br.skip(1)
        return 0, 1
    else:
        offset = RB_INDEX[6]
        peek = br.peek(11)
        for rv in range(min(15, zeros_left + 1)):
            idx = offset + rv
            if idx >= len(RB_SIZE):
                break
            sz = RB_SIZE[idx]
            code = RB_CODE[idx]
            if sz > 11:
                break
            peeked = (peek >> (11 - sz)) & ((1 << sz) - 1)
            if peeked == code:
                br.skip(sz)
                return rv, br.pos - start
        br.skip(1)
        return 0, 1


def decode_residual_block(br, nc, max_coeff=16, start_idx=0):
    """Decode one CAVLC residual block. Returns (coeffs[16], total_coeff, bits)."""
    start = br.pos
    tc, to, _ = decode_coeff_token(br, nc)
    coeffs = [0] * 16

    if tc == 0:
        return coeffs, 0, br.pos - start

    levels = []
    # Trailing ones
    for i in range(to):
        sign = br.bit()
        levels.append(-1 if sign else 1)

    # Remaining levels
    suffix_len = 0
    if tc > 10 and to < 3:
        suffix_len = 1

    for i in range(to, tc):
        level, _ = decode_level(br, suffix_len)
        # Adjust first non-trailing level
        if i == to and to < 3:
            if level > 0:
                level += 1
            else:
                level -= 1
        levels.append(level)

        # Update suffix_len
        if suffix_len == 0:
            suffix_len = 1
        if abs(levels[-1]) > (3 << (suffix_len - 1)):
            suffix_len += 1
            if suffix_len > 6:
                suffix_len = 6

    # Total zeros
    total_zeros = 0
    if tc < max_coeff:
        total_zeros, _ = decode_total_zeros(br, tc)

    # Run before
    zeros_left = total_zeros
    runs = [0] * tc
    for i in range(tc - 1):
        if zeros_left > 0:
            runs[i], _ = decode_run_before(br, zeros_left)
            zeros_left -= runs[i]
        else:
            runs[i] = 0
    if tc > 0:
        runs[tc - 1] = zeros_left

    # Place coefficients in zigzag order
    coeff_idx = tc + total_zeros - 1 + start_idx
    for i in range(tc):
        if coeff_idx < max_coeff:
            raster = ZIGZAG_4x4[coeff_idx] if coeff_idx < 16 else coeff_idx
            coeffs[raster] = levels[i]
        coeff_idx -= (runs[i] + 1)

    return coeffs, tc, br.pos - start


def remove_ep(data):
    """Remove emulation prevention bytes."""
    out = bytearray()
    i = 0
    while i < len(data):
        if i + 2 < len(data) and data[i] == 0 and data[i+1] == 0 and data[i+2] == 3:
            out.extend([0, 0])
            i += 3
        else:
            out.append(data[i])
            i += 1
    return bytes(out)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "tests/fixtures/baseline_640x480_short.h264"

    with open(path, "rb") as f:
        data = f.read()

    # Find all NALs
    nal_positions = []
    i = 0
    while i < len(data) - 4:
        if data[i:i+4] == b'\x00\x00\x00\x01':
            nal_positions.append(i + 4)
            i += 4
        elif data[i:i+3] == b'\x00\x00\x01':
            nal_positions.append(i + 3)
            i += 3
        else:
            i += 1
    print(f"Found {len(nal_positions)} NALs")
    for pos in nal_positions[:6]:
        print(f"  NAL at {pos}: type={data[pos] & 0x1F}")

    # Find IDR NAL
    for nal_pos in nal_positions:
        if (data[nal_pos] & 0x1F) == 5:
            i = nal_pos - 4
            print(f"IDR NAL at byte {nal_pos}")

            # Find NAL end
            end = nal_pos + 1
            for next_pos in nal_positions:
                if next_pos > nal_pos + 1:
                    end = next_pos - 4 if data[next_pos-4:next_pos-1] == b'\x00\x00\x01' else next_pos - 3
                    break
            else:
                end = len(data)

            rbsp = remove_ep(data[nal_pos+1:end])
            print(f"RBSP: {len(rbsp)} bytes")
            # Find NAL end
            end = i + 5
            while end < len(data) - 3:
                if data[end:end+3] == b'\x00\x00\x01':
                    break
                end += 1
            rbsp = remove_ep(data[i+5:end])
            br = BitReader(rbsp)

            # Slice header (simplified — matches our SPS: profile=66, bits_in_frame_num=4)
            first_mb = br.read_ue()
            slice_type = br.read_ue()
            pps_id = br.read_ue()
            frame_num = br.read_bits(4)  # bits_in_frame_num=4
            idr_pic_id = br.read_ue()
            br.bit()  # no_output_of_prior_pics
            br.bit()  # long_term_ref
            qp_delta = br.read_se()
            disable_deblock = br.read_ue()
            if disable_deblock != 1:
                br.read_se()  # alpha offset
                br.read_se()  # beta offset
            slice_qp = 26 + qp_delta  # PPS pic_init_qp=26

            print(f"Slice header: type={slice_type%5} qp_delta={qp_delta} end_bit={br.pos}")

            # Parse MBs
            for mb_addr in range(13):
                mb_start = br.pos
                mb_type = br.read_ue()
                mb_x = mb_addr % 40
                mb_y = mb_addr // 40

                if mb_type == 0:  # I_4x4
                    # Prediction modes
                    modes = []
                    for blk in range(16):
                        flag = br.bit()
                        if flag:
                            modes.append("M")
                        else:
                            rem = br.read_bits(3)
                            modes.append(str(rem))
                    chroma = br.read_ue()
                    cbp_code = br.read_ue()
                    cbp = CBP_INTRA[cbp_code] if cbp_code < 48 else 0
                    cbp_l = cbp & 0xF
                    cbp_c = (cbp >> 4) & 3
                    qp_d = 0
                    if cbp > 0:
                        qp_d = br.read_se()

                    # Decode residual
                    res_start = br.pos
                    for blk_idx in range(16):
                        bx = (blk_idx & 3) * 4
                        by = (blk_idx >> 2) * 4
                        group = (1 if by >= 8 else 0) * 2 + (1 if bx >= 8 else 0)
                        if (cbp_l >> group) & 1:
                            coeffs, tc, bits = decode_residual_block(br, 0, 16, 0)

                    # Chroma
                    if cbp_c >= 1:
                        decode_residual_block(br, -1, 4, 0)  # Cb DC
                        decode_residual_block(br, -1, 4, 0)  # Cr DC
                    if cbp_c >= 2:
                        for _ in range(4):
                            decode_residual_block(br, 0, 15, 1)  # Cb AC
                        for _ in range(4):
                            decode_residual_block(br, 0, 15, 1)  # Cr AC

                    print(f"  MB({mb_x},{mb_y}) I_4x4 bits={br.pos-mb_start} "
                          f"({mb_start}->{br.pos}) modes={''.join(modes)} "
                          f"cbp=0x{cbp:02x} qp_d={qp_d}")

                elif 1 <= mb_type <= 24:  # I_16x16
                    pred = (mb_type - 1) % 4
                    cbp_l = 15 if (mb_type - 1) // 4 >= 3 else 0
                    cbp_c = ((mb_type - 1) // 4) % 3
                    chroma = br.read_ue()
                    qp_d = br.read_se()

                    # DC block
                    dc_coeffs, dc_tc, dc_bits = decode_residual_block(br, 0, 16, 0)

                    # AC blocks (only if cbp_l > 0)
                    if cbp_l > 0:
                        for blk_idx in range(16):
                            decode_residual_block(br, 0, 15, 1)

                    # Chroma
                    if cbp_c >= 1:
                        decode_residual_block(br, -1, 4, 0)
                        decode_residual_block(br, -1, 4, 0)
                    if cbp_c >= 2:
                        for _ in range(4):
                            decode_residual_block(br, 0, 15, 1)
                        for _ in range(4):
                            decode_residual_block(br, 0, 15, 1)

                    print(f"  MB({mb_x},{mb_y}) I16x16 t={mb_type} bits={br.pos-mb_start} "
                          f"({mb_start}->{br.pos}) p={pred} cbpL={cbp_l} cbpC={cbp_c} "
                          f"dc_tc={dc_tc}")
                else:
                    print(f"  MB({mb_x},{mb_y}) type={mb_type} at bit {mb_start} — unsupported")
                    break

            break

    print("\nDone.")


if __name__ == "__main__":
    main()
