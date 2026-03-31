#!/usr/bin/env python3
"""Verify per-MB bit consumption for H.264 bitstream debugging.

Implements a minimal CAVLC decoder in Python to independently verify
bit consumption for specific MBs. Use --block-detail and --level-trace
to drill into specific blocks without editing the script.

Usage examples:
    # Show all MBs 0-12 with summary
    python scripts/verify_mb_decode.py tests/fixtures/baseline_640x480_short.h264

    # Show per-block detail for MB 9
    python scripts/verify_mb_decode.py --block-detail 9

    # Show level-by-level trace for MB 9 block 11
    python scripts/verify_mb_decode.py --level-trace 9:11

    # Parse MBs 5-15 with verbose output
    python scripts/verify_mb_decode.py --mb-range 5-15 --verbose

SPDX-License-Identifier: MIT
"""
import argparse
import sys

# ── VLC tables (must match cavlc_tables.hpp) ──────────────────────────────

COEFF_TOKEN_CODE = [
    [[1,5,7,7,7,7,15,11,8,15,11,15,11,15,11,7,4],[0,1,4,6,6,6,6,14,10,14,10,14,10,1,14,10,6],[0,0,1,5,5,5,5,5,13,9,13,9,13,9,13,9,5],[0,0,0,3,3,4,4,4,4,4,12,12,8,12,8,12,8]],
    [[3,11,7,7,7,4,7,15,11,15,11,8,15,11,7,9,7],[0,2,7,10,6,6,6,6,14,10,14,10,14,10,11,8,6],[0,0,3,9,5,5,5,5,13,9,13,9,13,9,6,10,5],[0,0,0,5,4,6,8,4,4,4,12,8,12,12,8,1,4]],
    [[15,15,11,8,15,11,9,8,15,11,15,11,8,13,9,5,1],[0,14,15,12,10,8,14,10,14,14,10,14,10,7,12,8,4],[0,0,13,14,11,9,13,9,13,10,13,9,13,9,11,7,3],[0,0,0,12,11,10,9,8,13,12,12,12,8,12,10,6,2]],
]
COEFF_TOKEN_SIZE = [
    [[1,6,8,9,10,11,13,13,13,14,14,15,15,16,16,16,16],[0,2,6,8,9,10,11,13,13,14,14,15,15,15,16,16,16],[0,0,3,7,8,9,10,11,13,13,14,14,15,15,16,16,16],[0,0,0,5,6,7,8,9,10,11,13,14,14,15,15,16,16]],
    [[2,6,6,7,8,8,9,11,11,12,12,12,13,13,13,14,14],[0,2,5,6,6,7,8,9,11,11,12,12,13,13,14,14,14],[0,0,3,6,6,7,8,9,11,11,12,12,13,13,13,14,14],[0,0,0,4,4,5,6,6,7,9,11,11,12,13,13,13,14]],
    [[4,6,6,6,7,7,7,7,8,8,9,9,9,10,10,10,10],[0,4,5,5,5,5,6,6,7,8,8,9,9,9,10,10,10],[0,0,4,5,5,5,6,6,7,7,8,8,9,9,10,10,10],[0,0,0,4,4,4,4,4,5,6,7,8,8,9,10,10,10]],
]
CHROMA_CODE = [[1,7,4,3,2],[0,1,6,3,3],[0,0,1,2,2],[0,0,0,5,0]]
CHROMA_SIZE = [[2,6,6,6,6],[0,1,6,7,8],[0,0,3,7,8],[0,0,0,6,7]]
CBP_INTRA = [47,31,15,0,23,27,29,30,7,11,13,14,39,43,45,46,16,3,5,10,12,19,21,26,28,35,37,42,44,1,2,4,8,17,18,20,24,6,9,22,25,32,33,34,36,40,38,41]
TZ_INDEX = [0,16,31,45,58,70,81,91,100,108,115,121,126,130,133]
TZ_SIZE = [1,3,3,4,4,5,5,6,6,7,7,8,8,9,9,9,3,3,3,3,3,4,4,4,4,5,5,6,6,6,6,4,3,3,3,4,4,3,3,4,5,5,6,5,6,5,3,4,4,3,3,3,4,3,4,5,5,5,4,4,4,3,3,3,3,3,4,5,4,5,6,5,3,3,3,3,3,3,4,3,6,6,5,3,3,3,2,3,4,3,6,6,4,5,3,2,2,3,3,6,6,6,4,2,2,3,2,5,5,5,3,2,2,2,4,4,4,3,3,1,3,4,4,2,1,3,3,3,1,2,2,2,1,1,1]
TZ_CODE = [1,3,2,3,2,3,2,3,2,3,2,3,2,3,2,1,7,6,5,4,3,5,4,3,2,3,2,3,2,1,0,5,7,6,5,4,3,4,3,2,3,2,1,1,0,3,7,5,4,6,5,4,3,3,2,2,1,0,5,4,3,7,6,5,4,3,2,1,1,0,1,1,7,6,5,4,3,2,1,1,0,1,1,5,4,3,3,2,1,1,0,1,1,1,3,3,2,2,1,0,1,0,1,3,2,1,1,1,1,0,1,3,2,1,1,0,1,1,2,1,3,0,1,1,1,1,0,1,1,1,0,1,1,0,1]
CHROMA_TZ_SIZE = [1,2,3,3,1,2,2,1,1]
CHROMA_TZ_CODE = [1,1,1,0,1,1,0,1,0]
CHROMA_TZ_INDEX = [0,4,7]
RB_INDEX = [0,2,5,9,14,20,27]
RB_SIZE = [1,1,1,2,2,2,2,2,2,2,2,2,3,3,2,2,3,3,3,3,2,3,3,3,3,3,3,3,3,3,3,3,3,3,4,5,6,7,8,9,10,11]
RB_CODE = [1,0,1,1,0,3,2,1,0,3,2,1,1,0,3,2,3,2,1,0,3,0,1,3,2,5,4,7,6,5,4,3,2,1,1,1,1,1,1,1,1,1]
ZIGZAG = [0,1,4,8,5,2,3,6,9,12,13,10,7,11,14,15]


class BitReader:
    def __init__(self, data):
        self.data = data
        self.pos = 0
    def bit(self):
        b = (self.data[self.pos//8] >> (7 - self.pos%8)) & 1
        self.pos += 1
        return b
    def peek(self, n):
        v = 0
        for i in range(n):
            o = self.pos + i
            v = (v << 1) | ((self.data[o//8] >> (7 - o%8)) & 1)
        return v
    def skip(self, n): self.pos += n
    def read_bits(self, n):
        v = 0
        for _ in range(n): v = (v << 1) | self.bit()
        return v
    def read_ue(self):
        lz = 0
        while self.bit() == 0: lz += 1
        v = 0
        for _ in range(lz): v = (v << 1) | self.bit()
        return (1 << lz) - 1 + v
    def read_se(self):
        ue = self.read_ue()
        return (ue+1)//2 if ue%2 else -(ue//2)


def _match_vlc(br, codes, sizes, n_to, n_tc, peek_bits):
    peek = br.peek(peek_bits)
    best_tc, best_to, best_sz = 0, 0, 255
    for to in range(n_to):
        for tc in range(n_tc):
            if to > tc: continue
            sz = sizes[to][tc]
            if sz == 0 or sz > peek_bits: continue
            code = codes[to][tc]
            if ((peek >> (peek_bits - sz)) & ((1 << sz) - 1)) == code and sz < best_sz:
                best_tc, best_to, best_sz = tc, to, sz
    br.skip(best_sz)
    return best_tc, best_to


def decode_coeff_token(br, nc):
    if nc < 0:
        return _match_vlc(br, CHROMA_CODE, CHROMA_SIZE, 4, 5, 8)
    if nc >= 8:
        code = br.read_bits(6); return code >> 2, code & 3
    idx = 0 if nc < 2 else (1 if nc < 4 else 2)
    return _match_vlc(br, COEFF_TOKEN_CODE[idx], COEFF_TOKEN_SIZE[idx], 4, 17, 16)


def decode_level(br, suffix_len, trace_fn=None):
    start = br.pos
    prefix = 0
    while br.bit() == 0: prefix += 1
    level_code = min(prefix, 15)

    if prefix < 14:
        if suffix_len > 0:
            suffix = br.read_bits(suffix_len)
            level_code = (level_code << suffix_len) | suffix
    elif prefix == 14:
        suf_sz = 4 if suffix_len == 0 else suffix_len
        suffix = br.read_bits(suf_sz)
        level_code = (level_code << (suf_sz if suffix_len == 0 else suffix_len)) | suffix
    else:
        suf_sz = prefix - 3
        suffix = br.read_bits(suf_sz)
        level_code = (15 << suffix_len) + suffix
        if suffix_len == 0: level_code += 15
        if prefix >= 16: level_code += (1 << (prefix - 3)) - 4096

    abs_level = (level_code + 2) >> 1
    sign = -1 if (level_code & 1) else 1
    level = abs_level * sign

    if trace_fn:
        trace_fn(f"      level: prefix={prefix} suffixLen={suffix_len} "
                 f"levelCode={level_code} abs={abs_level} sign={sign} "
                 f"-> {level} ({br.pos - start} bits)")
    return level


def decode_total_zeros(br, tc, is_chroma_dc=False):
    if tc == 0: return 0
    if is_chroma_dc:
        if tc > 3: return 0
        off = CHROMA_TZ_INDEX[tc-1]
        n = (CHROMA_TZ_INDEX[tc] if tc < 3 else 9) - off
        peek = br.peek(3)
        for v in range(n):
            sz, code = CHROMA_TZ_SIZE[off+v], CHROMA_TZ_CODE[off+v]
            if sz and ((peek >> (3-sz)) & ((1<<sz)-1)) == code:
                br.skip(sz); return v
        br.skip(1); return 0
    if tc >= 16: return 0
    off = TZ_INDEX[tc-1]
    n = (TZ_INDEX[tc] if tc < 15 else 135) - off
    peek = br.peek(9)
    for v in range(n):
        sz, code = TZ_SIZE[off+v], TZ_CODE[off+v]
        if sz == 0 or sz > 9: continue
        if ((peek >> (9-sz)) & ((1<<sz)-1)) == code:
            br.skip(sz); return v
    br.skip(1); return 0


def decode_run_before(br, zl):
    if zl == 0: return 0
    if zl <= 6:
        off = RB_INDEX[zl-1]; n = (RB_INDEX[zl] if zl < 6 else 27) - off
        peek = br.peek(3)
        for v in range(n):
            sz, code = RB_SIZE[off+v], RB_CODE[off+v]
            if ((peek >> (3-sz)) & ((1<<sz)-1)) == code: br.skip(sz); return v
        br.skip(1); return 0
    off = RB_INDEX[6]; peek = br.peek(11)
    for v in range(min(15, zl+1)):
        idx = off + v
        if idx >= len(RB_SIZE): break
        sz, code = RB_SIZE[idx], RB_CODE[idx]
        if sz > 11: break
        if ((peek >> (11-sz)) & ((1<<sz)-1)) == code: br.skip(sz); return v
    br.skip(1); return 0


def decode_residual(br, nc, max_coeff=16, start_idx=0, trace_fn=None, is_chroma_dc=False):
    start = br.pos
    tc, to = decode_coeff_token(br, nc)
    coeffs = [0]*16
    if tc == 0: return coeffs, 0, br.pos - start

    levels = []
    for i in range(to):
        sign = br.bit()
        levels.append(-1 if sign else 1)

    suffix_len = 1 if tc > 10 and to < 3 else 0
    for i in range(to, tc):
        level = decode_level(br, suffix_len, trace_fn)
        if i == to and to < 3:
            level += 1 if level > 0 else -1
        levels.append(level)
        if suffix_len == 0: suffix_len = 1
        if abs(levels[-1]) > (3 << (suffix_len - 1)):
            suffix_len = min(suffix_len + 1, 6)

    total_zeros = decode_total_zeros(br, tc, is_chroma_dc) if tc < max_coeff else 0
    zeros_left = total_zeros
    runs = [0]*tc
    for i in range(tc-1):
        runs[i] = decode_run_before(br, zeros_left) if zeros_left > 0 else 0
        zeros_left -= runs[i]
    if tc > 0: runs[tc-1] = zeros_left

    coeff_idx = tc + total_zeros - 1 + start_idx
    for i in range(tc):
        if 0 <= coeff_idx < 16:
            coeffs[ZIGZAG[coeff_idx]] = levels[i]
        coeff_idx -= (runs[i] + 1)

    return coeffs, tc, br.pos - start


def remove_ep(data):
    out = bytearray()
    i = 0
    while i < len(data):
        if i+2 < len(data) and data[i]==0 and data[i+1]==0 and data[i+2]==3:
            out.extend([0,0]); i += 3
        else: out.append(data[i]); i += 1
    return bytes(out)


def main():
    p = argparse.ArgumentParser(description="H.264 per-MB bit consumption verifier")
    p.add_argument("input", nargs="?", default="tests/fixtures/baseline_640x480_short.h264")
    p.add_argument("--mb-range", default="0-12", help="MB range to parse (e.g. 0-12)")
    p.add_argument("--block-detail", type=int, default=-1,
                   help="Show per-block residual info for this MB index")
    p.add_argument("--level-trace", default="",
                   help="Per-level trace for MB:block (e.g. 9:11)")
    p.add_argument("-v", "--verbose", action="store_true")
    args = p.parse_args()

    mb_start, mb_end = map(int, args.mb_range.split("-"))
    level_mb, level_blk = -1, -1
    if args.level_trace:
        parts = args.level_trace.split(":")
        level_mb = int(parts[0])
        level_blk = int(parts[1]) if len(parts) > 1 else -1

    with open(args.input, "rb") as f:
        data = f.read()

    # Find NALs
    nals = []
    i = 0
    while i < len(data) - 4:
        if data[i:i+4] == b'\x00\x00\x00\x01': nals.append(i+4); i += 4
        elif data[i:i+3] == b'\x00\x00\x01': nals.append(i+3); i += 3
        else: i += 1

    for nal_pos in nals:
        if (data[nal_pos] & 0x1F) != 5: continue  # Only IDR for now

        # Find NAL end
        end = len(data)
        for np in nals:
            if np > nal_pos + 1:
                end = np - (4 if data[np-4:np] == b'\x00\x00\x00\x01' else 3)
                break
        rbsp = remove_ep(data[nal_pos+1:end])
        br = BitReader(rbsp)

        # Simplified slice header (baseline profile, bits_in_frame_num=4)
        br.read_ue(); br.read_ue(); br.read_ue()  # first_mb, slice_type, pps_id
        br.read_bits(4)  # frame_num
        br.read_ue()     # idr_pic_id
        br.bit(); br.bit()  # no_output, long_term
        qp_d = br.read_se()
        dd = br.read_ue()
        if dd != 1: br.read_se(); br.read_se()
        print(f"Slice header end: bit {br.pos}, qp_delta={qp_d}")

        # NNZ tracking for luma nC context (40 MBs wide × 30 MBs tall × 16 blocks)
        WIDTH_MBS = 40
        nnz_luma = {}  # (mbAddr, blkIdx) -> totalCoeff

        def get_luma_nc(mb_a, blk_idx):
            """Compute nC from left and top neighbor NNZ."""
            bx, by = blk_idx & 3, blk_idx >> 2
            m_x, m_y = mb_a % WIDTH_MBS, mb_a // WIDTH_MBS
            left_nnz, top_nnz = 0, 0
            has_left, has_top = False, False
            # Left
            if bx > 0:
                left_nnz = nnz_luma.get((mb_a, blk_idx - 1), 0)
                has_left = True
            elif m_x > 0:
                left_nnz = nnz_luma.get((mb_a - 1, by * 4 + 3), 0)
                has_left = True
            # Top
            if by > 0:
                top_nnz = nnz_luma.get((mb_a, blk_idx - 4), 0)
                has_top = True
            elif m_y > 0:
                top_nnz = nnz_luma.get((mb_a - WIDTH_MBS, 12 + bx), 0)
                has_top = True
            if has_left and has_top:
                return (left_nnz + top_nnz + 1) >> 1
            if has_left: return left_nnz
            if has_top: return top_nnz
            return 0

        for mb_addr in range(mb_start, mb_end + 1):
            mb_s = br.pos
            mt = br.read_ue()
            mx, my = mb_addr % 40, mb_addr // 40
            detail = (mb_addr == args.block_detail)
            trace_this = (mb_addr == level_mb)

            if mt == 0:  # I_4x4
                modes = []
                for blk in range(16):
                    f = br.bit()
                    modes.append("M" if f else str(br.read_bits(3)))
                chroma = br.read_ue()
                cbp_code = br.read_ue()
                cbp = CBP_INTRA[cbp_code] if cbp_code < 48 else 0
                cbp_l, cbp_c = cbp & 0xF, (cbp >> 4) & 3
                qp = br.read_se() if cbp > 0 else 0

                for bi in range(16):
                    bx, by = (bi&3)*4, (bi>>2)*4
                    grp = (1 if by >= 8 else 0)*2 + (1 if bx >= 8 else 0)
                    if (cbp_l >> grp) & 1:
                        nc = get_luma_nc(mb_addr, bi)
                        bs = br.pos
                        tfn = print if (trace_this and (level_blk < 0 or level_blk == bi)) else None
                        coeffs, tc, bits = decode_residual(br, nc, 16, 0, tfn)
                        nnz_luma[(mb_addr, bi)] = tc
                        if detail or (trace_this and (level_blk < 0 or level_blk == bi)):
                            print(f"    blk{bi}: nC={nc} tc={tc} bits={bits} ({bs}->{br.pos})")
                if cbp_c >= 1:
                    for _ in range(2): decode_residual(br, -1, 4, 0, is_chroma_dc=True)
                if cbp_c >= 2:
                    for _ in range(8): decode_residual(br, 0, 15, 1)

                print(f"  MB({mx},{my}) I4x4 bits={br.pos-mb_s} ({mb_s}->{br.pos}) "
                      f"modes={''.join(modes)} cbp=0x{cbp:02x} qp_d={qp}")

            elif 1 <= mt <= 24:  # I_16x16
                pred = (mt-1)%4; cbp_l = 15 if (mt-1)//4 >= 3 else 0; cbp_c = ((mt-1)//4)%3
                br.read_ue(); br.read_se()  # chroma, qp_delta
                dc_c, dc_tc, dc_b = decode_residual(br, 0, 16, 0)
                if cbp_l > 0:
                    for _ in range(16): decode_residual(br, 0, 15, 1)
                if cbp_c >= 1:
                    for _ in range(2): decode_residual(br, -1, 4, 0, is_chroma_dc=True)
                if cbp_c >= 2:
                    for _ in range(8): decode_residual(br, 0, 15, 1)
                print(f"  MB({mx},{my}) I16x16 t={mt} bits={br.pos-mb_s} ({mb_s}->{br.pos}) "
                      f"p={pred} cbpL={cbp_l} cbpC={cbp_c} dc_tc={dc_tc}")
            else:
                print(f"  MB({mx},{my}) type={mt} at bit {mb_s}"); break
        break
    print("Done.")


if __name__ == "__main__":
    main()
