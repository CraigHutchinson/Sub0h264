#!/usr/bin/env python3
"""H.264 bitstream analysis utility.

Parses an Annex-B H.264 file and dumps per-MB decode information
for debugging decoder correctness. Useful for comparing against
our C++ decoder's trace output.

Usage:
    python scripts/parse_h264_bitstream.py tests/fixtures/baseline_640x480_short.h264 [--mb-range 0-12]

SPDX-License-Identifier: MIT
"""
import argparse
import sys


def remove_emulation_prevention(data: bytes) -> bytearray:
    """Remove 00 00 03 emulation prevention bytes from RBSP."""
    rbsp = bytearray()
    i = 0
    while i < len(data):
        if i + 2 < len(data) and data[i] == 0 and data[i+1] == 0 and data[i+2] == 3:
            rbsp.extend([0, 0])
            i += 3
        else:
            rbsp.append(data[i])
            i += 1
    return rbsp


class BitReader:
    """Simple bit-level reader for RBSP data."""
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def bit(self, off=None):
        if off is None:
            off = self.pos
        return (self.data[off // 8] >> (7 - off % 8)) & 1

    def read_bit(self):
        b = self.bit()
        self.pos += 1
        return b

    def read_bits(self, n):
        val = 0
        for _ in range(n):
            val = (val << 1) | self.read_bit()
        return val

    def read_ue(self):
        """Read unsigned Exp-Golomb coded value."""
        lz = 0
        while self.bit() == 0:
            lz += 1
            self.pos += 1
        self.pos += 1  # skip the 1 bit
        val = 0
        for _ in range(lz):
            val = (val << 1) | self.read_bit()
        return (1 << lz) - 1 + val

    def read_se(self):
        """Read signed Exp-Golomb coded value."""
        ue = self.read_ue()
        if ue % 2 == 0:
            return -(ue // 2)
        else:
            return (ue + 1) // 2


def find_nals(data: bytes):
    """Find NAL unit boundaries in Annex-B byte stream."""
    nals = []
    i = 0
    while i < len(data) - 4:
        if data[i:i+4] == b'\x00\x00\x00\x01':
            nals.append(i + 4)
            i += 4
        elif data[i:i+3] == b'\x00\x00\x01':
            nals.append(i + 3)
            i += 3
        else:
            i += 1
    return nals


def parse_sps_basic(br: BitReader):
    """Parse basic SPS fields."""
    profile = br.read_bits(8)
    br.read_bits(8)  # constraint flags
    level = br.read_bits(8)
    sps_id = br.read_ue()
    log2_max_frame_num = br.read_ue() + 4
    poc_type = br.read_ue()
    return {
        "profile": profile, "level": level, "sps_id": sps_id,
        "log2_max_frame_num": log2_max_frame_num, "poc_type": poc_type,
        "bits_in_frame_num": log2_max_frame_num,
    }


def parse_slice_header(br: BitReader, sps: dict, is_idr: bool):
    """Parse slice header fields."""
    first_mb = br.read_ue()
    slice_type = br.read_ue()
    pps_id = br.read_ue()
    frame_num = br.read_bits(sps["bits_in_frame_num"])

    idr_pic_id = 0
    if is_idr:
        idr_pic_id = br.read_ue()

    # dec_ref_pic_marking for IDR
    if is_idr:
        br.read_bit()  # no_output_of_prior_pics_flag
        br.read_bit()  # long_term_reference_flag

    # slice_qp_delta
    qp_delta = br.read_se()

    # deblocking_filter_control_present (assume PPS has it)
    disable_deblock = br.read_ue()
    alpha_offset = 0
    beta_offset = 0
    if disable_deblock != 1:
        alpha_offset = br.read_se() * 2
        beta_offset = br.read_se() * 2

    return {
        "first_mb": first_mb, "slice_type": slice_type % 5,
        "pps_id": pps_id, "frame_num": frame_num,
        "qp_delta": qp_delta, "disable_deblock": disable_deblock,
        "header_end_bit": br.pos,
    }


# CBP table from H.264 Table 9-4 (intra column)
CBP_INTRA = [
    47, 31, 15, 0, 23, 27, 29, 30, 7, 11, 13, 14,
    39, 43, 45, 46, 16, 3, 5, 10, 12, 19, 21, 26,
    28, 35, 37, 42, 44, 1, 2, 4, 8, 17, 18, 20,
    24, 6, 9, 22, 25, 32, 33, 34, 36, 40, 38, 41,
]


def parse_i4x4_mb(br: BitReader, mb_x: int, mb_y: int, verbose: bool):
    """Parse an I_4x4 macroblock and return mode/cbp information."""
    modes = []
    for blk in range(16):
        prev_flag = br.read_bit()
        if prev_flag:
            modes.append(("mpm", None))
        else:
            rem = br.read_bits(3)
            modes.append(("rem", rem))

    chroma_pred = br.read_ue()
    cbp_code = br.read_ue()
    cbp = CBP_INTRA[cbp_code] if cbp_code < 48 else 0
    cbp_luma = cbp & 0x0F
    cbp_chroma = (cbp >> 4) & 0x03

    qp_delta = 0
    if cbp > 0:
        qp_delta = br.read_se()

    if verbose:
        print(f"    modes: ", end="")
        for i, (typ, val) in enumerate(modes):
            if typ == "mpm":
                print("M", end="")
            else:
                print(f"{val}", end="")
            if i < 15:
                print(" ", end="")
        print()
        print(f"    chroma_pred={chroma_pred} cbpCode={cbp_code} cbp=0x{cbp:02x} "
              f"(L={cbp_luma} C={cbp_chroma}) qp_delta={qp_delta}")

    return {"cbp": cbp, "cbp_luma": cbp_luma, "cbp_chroma": cbp_chroma,
            "qp_delta": qp_delta, "modes": modes, "end_bit": br.pos}


def parse_i16x16_mb(br: BitReader, mb_type: int, verbose: bool):
    """Parse an I_16x16 macroblock."""
    pred_mode = (mb_type - 1) % 4
    cbp_luma = 15 if ((mb_type - 1) // 4) >= 3 else 0
    cbp_chroma = ((mb_type - 1) // 4) % 3

    chroma_pred = br.read_ue()
    qp_delta = br.read_se()

    if verbose:
        print(f"    pred={pred_mode} cbpL={cbp_luma} cbpC={cbp_chroma} "
              f"chroma={chroma_pred} qp_delta={qp_delta}")

    return {"pred_mode": pred_mode, "cbp_luma": cbp_luma,
            "cbp_chroma": cbp_chroma, "qp_delta": qp_delta, "end_bit": br.pos}


def main():
    parser = argparse.ArgumentParser(description="H.264 bitstream analyzer")
    parser.add_argument("input", help="H.264 Annex-B file")
    parser.add_argument("--mb-range", default="0-12",
                        help="MB range to analyze (e.g., 0-12)")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Show per-block details")
    args = parser.parse_args()

    mb_start, mb_end = map(int, args.mb_range.split("-"))

    with open(args.input, "rb") as f:
        data = f.read()

    nal_offsets = find_nals(data)
    print(f"Found {len(nal_offsets)} NAL units")

    sps = None
    for offset in nal_offsets:
        nal_type = data[offset] & 0x1F
        if nal_type == 7:  # SPS
            rbsp = remove_emulation_prevention(data[offset+1:offset+50])
            br = BitReader(rbsp)
            sps = parse_sps_basic(br)
            print(f"SPS: profile={sps['profile']} level={sps['level']} "
                  f"bits_in_frame_num={sps['bits_in_frame_num']}")

        elif nal_type == 5 and sps:  # IDR
            # Find NAL end
            nal_end = len(data)
            for next_off in nal_offsets:
                if next_off > offset + 1:
                    # Check if there's a start code before this offset
                    check = next_off - 3
                    if check > offset and data[check:check+3] == b'\x00\x00\x01':
                        nal_end = check
                        break
                    check = next_off - 4
                    if check > offset and data[check:check+4] == b'\x00\x00\x00\x01':
                        nal_end = check
                        break

            rbsp = remove_emulation_prevention(data[offset+1:nal_end])
            br = BitReader(rbsp)

            sh = parse_slice_header(br, sps, is_idr=True)
            print(f"\nIDR slice: type={sh['slice_type']} first_mb={sh['first_mb']} "
                  f"qp_delta={sh['qp_delta']} header_end_bit={sh['header_end_bit']}")

            # Parse MBs
            for mb_addr in range(mb_start, mb_end + 1):
                mb_start_bit = br.pos
                mb_type = br.read_ue()
                mb_type_bit = br.pos

                mb_x = mb_addr % 40  # Assume 640/16 = 40 MBs wide
                mb_y = mb_addr // 40

                if mb_type == 0:  # I_4x4
                    print(f"  MB({mb_x},{mb_y}) mb_type=0 (I_4x4) start_bit={mb_start_bit}")
                    info = parse_i4x4_mb(br, mb_x, mb_y, args.verbose)
                    print(f"    end_bit={info['end_bit']} ({info['end_bit']-mb_start_bit} bits)")
                elif 1 <= mb_type <= 24:  # I_16x16
                    print(f"  MB({mb_x},{mb_y}) mb_type={mb_type} (I_16x16) start_bit={mb_start_bit}")
                    info = parse_i16x16_mb(br, mb_type, args.verbose)
                    # Skip DC block + AC blocks + chroma based on cbp
                    # (simplified — just report what we parsed)
                    print(f"    after_header_bit={info['end_bit']} ({info['end_bit']-mb_start_bit} bits for header)")
                    # TODO: parse residual to get full MB bit consumption
                    break  # Can't continue without residual parsing
                else:
                    print(f"  MB({mb_x},{mb_y}) mb_type={mb_type} start_bit={mb_start_bit}")
                    break

            break  # Only process first IDR

    print("\nDone.")


if __name__ == "__main__":
    main()
