#!/usr/bin/env python3
"""Verify the first CABAC bin decode for an H.264 IDR frame.

Reads the slice header, initializes the CABAC engine, and traces the
first decodeBin call to verify whether mb_type bin 0 should be 0 or 1.

Usage:
    python scripts/verify_cabac_first_bin.py <h264_file>

SPDX-License-Identifier: MIT
"""
import argparse
import sys


# rangeTabLPS from the H.264 spec Table 9-45 (64 states × 4 qRangeIdx).
# Each row = [qRange0, qRange1, qRange2, qRange3].
RANGE_TAB_LPS = [
    [128,176,208,240],[128,167,197,227],[128,158,187,216],[123,150,178,205],
    [116,142,169,195],[111,135,160,185],[105,128,152,175],[100,122,144,166],
    [ 95,116,137,158],[ 90,110,130,150],[ 85,104,123,142],[ 81, 99,117,135],
    [ 77, 94,111,128],[ 73, 89,105,122],[ 69, 85,100,116],[ 66, 80, 95,110],
    [ 62, 76, 90,104],[ 59, 72, 86, 99],[ 56, 69, 81, 94],[ 53, 65, 77, 89],
    [ 51, 62, 73, 85],[ 48, 59, 69, 80],[ 46, 56, 66, 76],[ 43, 53, 63, 72],
    [ 41, 50, 59, 69],[ 39, 48, 56, 65],[ 37, 45, 54, 62],[ 35, 43, 51, 59],
    [ 33, 41, 48, 56],[ 32, 39, 46, 53],[ 30, 37, 43, 50],[ 29, 35, 41, 48],
    [ 27, 33, 39, 45],[ 26, 31, 37, 43],[ 24, 30, 35, 41],[ 23, 28, 33, 39],
    [ 22, 27, 32, 37],[ 21, 26, 30, 35],[ 20, 24, 29, 33],[ 19, 23, 27, 31],
    [ 18, 22, 26, 30],[ 17, 21, 25, 28],[ 16, 20, 23, 27],[ 15, 19, 22, 25],
    [ 14, 18, 21, 24],[ 14, 17, 20, 23],[ 13, 16, 19, 22],[ 12, 15, 18, 21],
    [ 12, 14, 17, 20],[ 11, 14, 16, 19],[ 11, 13, 15, 18],[ 10, 12, 15, 17],
    [ 10, 12, 14, 16],[  9, 11, 13, 15],[  9, 11, 12, 14],[  8, 10, 12, 14],
    [  8,  9, 11, 13],[  7,  9, 11, 12],[  7,  9, 10, 12],[  7,  8, 10, 11],
    [  6,  8,  9, 11],[  6,  7,  9, 10],[  6,  7,  8,  9],[  2,  2,  2,  2],
]


def find_nal_units(data):
    nals = []
    i = 0
    while i < len(data) - 3:
        if data[i] == 0 and data[i+1] == 0:
            if data[i+2] == 1:
                nals.append(i + 3)
                i += 3
                continue
            elif data[i+2] == 0 and i+3 < len(data) and data[i+3] == 1:
                nals.append(i + 4)
                i += 4
                continue
        i += 1
    return nals


def remove_emulation(ebsp):
    rbsp = bytearray()
    zero_count = 0
    for b in ebsp:
        if zero_count == 2 and b == 3:
            zero_count = 0
            continue
        if b == 0:
            zero_count += 1
        else:
            zero_count = 0
        rbsp.append(b)
    return bytes(rbsp)


def compute_init_state(m, n, qp):
    pre = ((m * qp) >> 4) + n
    pre = max(1, min(126, pre))
    if pre <= 63:
        return (63 - pre, 0)  # (pStateIdx, valMPS)
    else:
        return (pre - 64, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("h264_file")
    args = parser.parse_args()

    data = open(args.h264_file, "rb").read()
    nals = find_nal_units(data)

    for nal_offset in nals:
        nal_type = data[nal_offset] & 0x1F
        if nal_type != 5:  # IDR
            continue

        rbsp = remove_emulation(data[nal_offset + 1:])
        print(f"IDR NAL at offset {nal_offset}, RBSP length {len(rbsp)}")

        # Parse slice header bits manually
        # Assuming: first_mb=0(1bit), slice_type=7(7bits), pps_id=0(1bit),
        # frame_num=0(4bits), idr_pic_id=0(1bit), no_output=0(1bit),
        # long_term=0(1bit), qp_delta=se(-3)=ue(6)(5bits),
        # deblock=ue(0)(1bit), alpha=se(0)(1bit), beta=se(0)(1bit)
        # Total: 24 bits = 3 bytes

        # CABAC data starts at byte 3
        cabac_byte0 = rbsp[3]
        cabac_byte1 = rbsp[4]

        # codIOffset = first 9 bits
        codIOffset = (cabac_byte0 << 1) | (cabac_byte1 >> 7)
        codIRange = 510

        print(f"  CABAC byte 3: 0x{cabac_byte0:02x} = {cabac_byte0:08b}")
        print(f"  CABAC byte 4: 0x{cabac_byte1:02x} = {cabac_byte1:08b}")
        print(f"  codIRange = {codIRange}")
        print(f"  codIOffset = {codIOffset} (9 bits: {codIOffset:09b})")

        # Context for mb_type bin 0: ctxIdx 5 (ctxIdxInc=2, first MB, no neighbors)
        # I-slice init: (m=3, n=74) at QP=17
        qp = 17  # pic_init_qp=20 + qp_delta=-3
        m, n = 3, 74
        pStateIdx, valMPS = compute_init_state(m, n, qp)
        print(f"\n  Context 5: m={m}, n={n}, QP={qp}")
        print(f"  pStateIdx={pStateIdx}, valMPS={valMPS}")

        # rangeLPS from spec table
        qRangeIdx = (codIRange >> 6) & 3
        rangeLPS = RANGE_TAB_LPS[pStateIdx][qRangeIdx]
        print(f"  qRangeIdx={qRangeIdx}, rangeLPS={rangeLPS}")

        codIRange_after = codIRange - rangeLPS
        print(f"  codIRange after subtract: {codIRange_after}")
        print(f"  codIOffset ({codIOffset}) >= codIRange ({codIRange_after})?  "
              f"{'YES -> LPS' if codIOffset >= codIRange_after else 'NO -> MPS'}")

        if codIOffset >= codIRange_after:
            symbol = 1 - valMPS
            print(f"  Decoded: LPS -> symbol={symbol} (I_4x4)" if symbol == 0 else f"  Decoded: LPS -> symbol={symbol} (I_16x16)")
        else:
            symbol = valMPS
            print(f"  Decoded: MPS -> symbol={symbol} (I_16x16)" if symbol == 1 else f"  Decoded: MPS -> symbol={symbol} (I_4x4)")

        break


if __name__ == "__main__":
    main()
