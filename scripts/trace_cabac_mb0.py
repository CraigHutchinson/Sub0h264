#!/usr/bin/env python3
"""Trace every CABAC bin for MB(0,0) of an I-slice, comparing against
the spec arithmetic to verify correctness.

Implements a pure-Python CABAC engine and traces bin-by-bin for:
- mb_type
- 16 x prev_intra_pred_mode_flag (+ rem bypass)
- intra_chroma_pred_mode
- coded_block_pattern (4 luma + chroma)
- mb_qp_delta
- first residual block coded_block_flag

Usage:
    python scripts/trace_cabac_mb0.py <h264_file> [--slice-qp QP]

SPDX-License-Identifier: MIT
"""
import argparse
import sys

# rangeTabLPS from H.264 spec Table 9-45
RANGE_TAB_LPS = [
    [128,176,208,240],[128,167,197,227],[128,158,187,216],[123,150,178,205],
    [116,142,169,195],[111,135,160,185],[105,128,152,175],[100,122,144,166],
    [95,116,137,158],[90,110,130,150],[85,104,123,142],[81,99,117,135],
    [77,94,111,128],[73,89,105,122],[69,85,100,116],[66,80,95,110],
    [62,76,90,104],[59,72,86,99],[56,69,81,94],[53,65,77,89],
    [51,62,73,85],[48,59,69,80],[46,56,66,76],[43,53,63,72],
    [41,50,59,69],[39,48,56,65],[37,45,54,62],[35,43,51,59],
    [33,41,48,56],[32,39,46,53],[30,37,43,50],[29,35,41,48],
    [27,33,39,45],[26,31,37,43],[24,30,35,41],[23,28,33,39],
    [22,27,32,37],[21,26,30,35],[20,24,29,33],[19,23,27,31],
    [18,22,26,30],[17,21,25,28],[16,20,23,27],[15,19,22,25],
    [14,18,21,24],[14,17,20,23],[13,16,19,22],[12,15,18,21],
    [12,14,17,20],[11,14,16,19],[11,13,15,18],[10,12,15,17],
    [10,12,14,16],[9,11,13,15],[9,11,12,14],[8,10,12,14],
    [8,9,11,13],[7,9,11,12],[7,9,10,12],[7,8,10,11],
    [6,8,9,11],[6,7,9,10],[6,7,8,9],[2,2,2,2],
]

# State transitions
TRANS_MPS = [
    1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,
    33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,62,63
]
TRANS_LPS = [
    0,0,1,2,2,4,4,5,6,7,8,9,9,11,11,12,13,13,15,15,16,16,18,18,19,19,21,21,22,22,23,24,
    24,25,26,26,27,27,28,29,29,30,30,30,31,32,32,33,33,33,34,34,35,35,35,36,36,36,37,37,37,38,38,63
]


def compute_init_state(m, n, qp):
    pre = ((m * qp) >> 4) + n
    pre = max(1, min(126, pre))
    if pre <= 63:
        return (63 - pre, 0)
    else:
        return (pre - 64, 1)


class CabacEngine:
    def __init__(self, rbsp_bytes, bit_offset):
        self.data = rbsp_bytes
        self.bit_pos = bit_offset
        self.range = 510
        self.offset = self._read_bits(9)
        self.bin_count = 0

    def _read_bit(self):
        byte_idx = self.bit_pos >> 3
        bit_idx = 7 - (self.bit_pos & 7)
        self.bit_pos += 1
        if byte_idx < len(self.data):
            return (self.data[byte_idx] >> bit_idx) & 1
        return 0

    def _read_bits(self, n):
        val = 0
        for _ in range(n):
            val = (val << 1) | self._read_bit()
        return val

    def _renormalize(self):
        while self.range < 256:
            self.range <<= 1
            self.offset = (self.offset << 1) | self._read_bit()

    def decode_bin(self, p_state, mps):
        q_range = (self.range >> 6) & 3
        range_lps = RANGE_TAB_LPS[p_state][q_range]
        self.range -= range_lps

        if self.offset >= self.range:
            # LPS
            symbol = 1 - mps
            self.offset -= self.range
            self.range = range_lps
            new_p = TRANS_LPS[p_state]
            new_mps = mps
            if p_state == 0:
                new_mps = 1 - mps
        else:
            # MPS
            symbol = mps
            new_p = TRANS_MPS[p_state]
            new_mps = mps

        self._renormalize()
        self.bin_count += 1
        return symbol, new_p, new_mps

    def decode_bypass(self):
        self.offset = (self.offset << 1) | self._read_bit()
        if self.offset >= self.range:
            self.offset -= self.range
            self.bin_count += 1
            return 1
        self.bin_count += 1
        return 0

    def decode_terminate(self):
        self.range -= 2
        if self.offset >= self.range:
            self.bin_count += 1
            return 1
        self._renormalize()
        self.bin_count += 1
        return 0


# I-slice init (m,n) values for contexts 0-99
# Extracted from cCabacInitMN[3]
INIT_MN_I = [
    (20,-15),(2,54),(3,74),(20,-15),(2,54),(3,74),(-28,127),(-23,104),
    (-6,53),(-1,54),(7,51),(23,33),(23,2),(21,0),(1,9),(0,49),
    (-37,118),(5,57),(-13,78),(-11,65),(1,62),(12,49),(-4,73),(17,50),
    (18,64),(9,43),(29,0),(26,67),(16,90),(9,104),(-46,127),(-20,104),
    (1,67),(-13,78),(-11,65),(1,62),(-6,86),(-17,95),(-6,61),(9,45),
    (-3,69),(-6,81),(-11,96),(6,55),(7,67),(-5,86),(2,88),(0,58),
    (-3,76),(-10,94),(5,54),(4,69),(-3,81),(0,88),(-7,67),(-5,74),
    (-4,74),(-5,80),(-7,72),(1,58),(0,41),(0,63),(0,63),(0,63),
    (-9,83),(4,86),(0,97),(-7,72),(13,41),(3,62),(0,11),(1,55),
    (0,69),(-17,127),(-13,102),(0,82),(-7,74),(-21,107),(-27,127),(-31,127),
    (-24,127),(-18,95),(-27,127),(-21,114),(-30,127),(-17,123),(-12,115),(-16,122),
    (-11,115),(-12,63),(-2,68),(-15,84),(-13,104),(-3,70),(-8,93),(-10,90),
    (-30,127),(-1,74),(-6,97),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("h264_file")
    parser.add_argument("--slice-qp", type=int, default=17)
    args = parser.parse_args()

    data = open(args.h264_file, "rb").read()

    # Find IDR NAL
    nals = []
    i = 0
    while i < len(data) - 3:
        if data[i] == 0 and data[i+1] == 0:
            if data[i+2] == 1:
                nals.append(i+3)
                i += 3
            elif data[i+2] == 0 and i+3 < len(data) and data[i+3] == 1:
                nals.append(i+4)
                i += 4
            else:
                i += 1
        else:
            i += 1

    rbsp = None
    for nal_off in nals:
        if (data[nal_off] & 0x1F) == 5:
            rbsp = data[nal_off + 1:]
            break

    if rbsp is None:
        print("No IDR NAL found")
        return

    # Init contexts
    qp = args.slice_qp
    ctx = {}
    for i, (m, n) in enumerate(INIT_MN_I):
        ctx[i] = compute_init_state(m, n, qp)

    # CABAC engine starts at RBSP byte 3 (after 24-bit slice header)
    engine = CabacEngine(rbsp, 24)
    print(f"CABAC init: range={engine.range}, offset={engine.offset}")

    # 1. mb_type bin 0
    p, mps = ctx[5]  # ctxIdx 5 for MB(0,0) ctxInc=2
    sym, p, mps = engine.decode_bin(p, mps)
    ctx[5] = (p, mps)
    mb_type = "I_4x4" if sym == 0 else "I_16x16+"
    print(f"Bin {engine.bin_count}: mb_type bin0 = {sym} -> {mb_type}")

    if sym != 0:
        print("I_16x16: stopping trace (different syntax)")
        return

    # 2. 16 x prev_intra4x4_pred_mode
    for blk in range(16):
        p68, mps68 = ctx[68]
        flag, p68, mps68 = engine.decode_bin(p68, mps68)
        ctx[68] = (p68, mps68)
        if flag == 1:
            print(f"  blk{blk}: prev_flag=1 (MPM)")
        else:
            rem = 0
            for b in range(3):
                rem = (rem << 1) | engine.decode_bypass()
            print(f"  blk{blk}: prev_flag=0, rem={rem}")

    # 3. intra_chroma_pred_mode (TU)
    p64, mps64 = ctx[64]  # ctxIdx 64+ctxInc(0)
    cm_bin0, p64, mps64 = engine.decode_bin(p64, mps64)
    ctx[64] = (p64, mps64)
    if cm_bin0 == 0:
        chroma_mode = 0
    else:
        p67, mps67 = ctx[67]  # ctxIdx 64+3
        cm_bin1, p67, mps67 = engine.decode_bin(p67, mps67)
        ctx[67] = (p67, mps67)
        if cm_bin1 == 0:
            chroma_mode = 1
        else:
            cm_bin2, p67, mps67 = engine.decode_bin(p67, mps67)
            ctx[67] = (p67, mps67)
            chroma_mode = 2 if cm_bin2 == 0 else 3
    print(f"Bin {engine.bin_count}: chroma_mode={chroma_mode}")

    # 4. CBP (4 luma + chroma)
    cbp_luma = 0
    for i in range(4):
        ctx_inc = 0  # simplified: first MB no neighbors
        if i == 1:
            ctx_inc = (0 if (cbp_luma & 1) else 1) + 0
        elif i == 2:
            ctx_inc = 0 + (0 if (cbp_luma & 1) else 2)
        elif i == 3:
            ctx_inc = (0 if (cbp_luma & 4) else 1) + (0 if (cbp_luma & 2) else 2)
        p_cbp, mps_cbp = ctx[73 + ctx_inc]
        cbp_bin, p_cbp, mps_cbp = engine.decode_bin(p_cbp, mps_cbp)
        ctx[73 + ctx_inc] = (p_cbp, mps_cbp)
        if cbp_bin:
            cbp_luma |= (1 << i)

    # Chroma CBP
    p77, mps77 = ctx[77]
    cc_bin0, p77, mps77 = engine.decode_bin(p77, mps77)
    ctx[77] = (p77, mps77)
    cbp_chroma = 0
    if cc_bin0:
        p81, mps81 = ctx[81]
        cc_bin1, p81, mps81 = engine.decode_bin(p81, mps81)
        ctx[81] = (p81, mps81)
        cbp_chroma = 1 if cc_bin1 == 0 else 2

    cbp = cbp_luma | (cbp_chroma << 4)
    print(f"Bin {engine.bin_count}: CBP=0x{cbp:02x} (luma={cbp_luma:04b} chroma={cbp_chroma})")

    # 5. QP delta (if CBP > 0)
    if cbp > 0:
        p60, mps60 = ctx[60]
        qpd_bin0, p60, mps60 = engine.decode_bin(p60, mps60)
        ctx[60] = (p60, mps60)
        if qpd_bin0 == 0:
            print(f"Bin {engine.bin_count}: qp_delta=0")
        else:
            abs_val = 1
            while abs_val < 10:
                ci = 62 + (1 if abs_val > 1 else 0)
                p_qp, mps_qp = ctx[ci]
                qpd_bin, p_qp, mps_qp = engine.decode_bin(p_qp, mps_qp)
                ctx[ci] = (p_qp, mps_qp)
                if qpd_bin == 0:
                    break
                abs_val += 1
            # Map: odd->positive, even->negative
            delta = ((abs_val + 1) >> 1) * (1 if abs_val & 1 else -1)
            print(f"Bin {engine.bin_count}: qp_delta={delta} (abs={abs_val})")

    # 6. First residual block coded_block_flag
    p93, mps93 = ctx[93]  # ctxCbf + cat*4 + cbfCtxInc(0)
    cbf_bin, p93, mps93 = engine.decode_bin(p93, mps93)
    ctx[93] = (p93, mps93)
    print(f"Bin {engine.bin_count}: coded_block_flag[0] = {cbf_bin}")
    print(f"\nTotal bins consumed: {engine.bin_count}")
    print(f"Bitstream position: bit {engine.bit_pos}")


if __name__ == "__main__":
    main()
