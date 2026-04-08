#!/usr/bin/env python3
"""Generate per-bin CABAC trace matching C++ format for comparison.

Outputs: binIdx ctxState symbol range offset (post-decode values)
Only logs context-coded bins (not bypass), matching C++ bin trace.

SPDX-License-Identifier: MIT
"""
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
    def __init__(self, data, bit_offset):
        self.data = data
        self.bit_pos = bit_offset
        self.range = 510
        self.offset = self._read_bits(9)
        self.bin_idx = 0

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
        # Compute combined_state for trace (matches C++ format)
        combined_state = (mps << 6) | p_state

        q_range = (self.range >> 6) & 3
        range_lps = RANGE_TAB_LPS[p_state][q_range]
        self.range -= range_lps

        if self.offset >= self.range:
            symbol = 1 - mps
            self.offset -= self.range
            self.range = range_lps
            new_p = TRANS_LPS[p_state]
            new_mps = mps
            if p_state == 0:
                new_mps = 1 - mps
        else:
            symbol = mps
            new_p = TRANS_MPS[p_state]
            new_mps = mps

        self._renormalize()

        new_combined = (new_mps << 6) | new_p
        print(f"{self.bin_idx} {combined_state} {new_combined} {symbol} {self.range} {self.offset}")
        self.bin_idx += 1

        return symbol, new_p, new_mps

    def decode_bypass(self):
        self.offset = (self.offset << 1) | self._read_bit()
        if self.offset >= self.range:
            self.offset -= self.range
            return 1
        return 0


def load_init_mn(path="components/sub0h264/src/cabac_init_mn.hpp", idc=3):
    import re
    with open(path) as f:
        content = f.read()
    sections = content.split("// init_idc ")
    for s in sections[1:]:
        if s.startswith(str(idc)):
            pairs = re.findall(r'\{\s*(-?\d+),\s*(-?\d+)\}', s)
            return [(int(m), int(n)) for m, n in pairs[:460]]
    return []


def main():
    h264_file = sys.argv[1] if len(sys.argv) > 1 else "tests/fixtures/cabac_flat_main.h264"
    qp = int(sys.argv[2]) if len(sys.argv) > 2 else 17

    data = open(h264_file, "rb").read()

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
    init_mn = load_init_mn()
    ctx = {}
    for i, (m, n) in enumerate(init_mn):
        ctx[i] = compute_init_state(m, n, qp)

    # Print initial state of key contexts
    p5, m5 = ctx[5]
    print(f"# ctx[5] init: pState={p5}, mps={m5}, combined={(m5<<6)|p5}")
    p68, m68 = ctx[68]
    print(f"# ctx[68] init: pState={p68}, mps={m68}, combined={(m68<<6)|p68}")
    print(f"# Slice header assumed: 24 bits")
    print(f"# binIdx ctxState newState symbol range offset")

    engine = CabacEngine(rbsp, 24)
    print(f"# CABAC init: range={engine.range}, offset={engine.offset}")

    # mb_type
    p, mps = ctx[5]
    sym, p, mps = engine.decode_bin(p, mps)
    ctx[5] = (p, mps)
    if sym != 0:
        print("# Not I_4x4, stopping")
        return

    # 16 prev_intra4x4
    for blk in range(16):
        p68, mps68 = ctx[68]
        flag, p68, mps68 = engine.decode_bin(p68, mps68)
        ctx[68] = (p68, mps68)
        if flag == 0:
            # 3 bypass bins for rem
            rem = 0
            for b in range(3):
                rem = (rem << 1) | engine.decode_bypass()
            print(f"# blk{blk}: flag=0, rem={rem}")
        else:
            print(f"# blk{blk}: flag=1 (MPM)")

    # Stop after prediction modes for comparison
    print(f"# Done: bit_pos={engine.bit_pos}")


if __name__ == "__main__":
    main()
