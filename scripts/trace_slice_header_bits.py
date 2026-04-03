#!/usr/bin/env python3
"""Parse and trace the slice header of a specific frame in an H.264 file.

Reads the raw RBSP of the specified frame's slice NAL, decodes each
slice header field per ITU-T H.264 section 7.3.3, and reports bit positions.

Usage:
    python scripts/trace_slice_header_bits.py --fixture pan_up --frame 16
    python scripts/trace_slice_header_bits.py --fixture scrolling_texture --frame 1

Requires: the fixture .h264 file in tests/fixtures/
"""
import argparse


def find_nal_starts(data):
    """Find all NAL unit start positions (byte after start code)."""
    starts = []
    i = 0
    while i < len(data) - 4:
        if data[i:i+4] == b'\x00\x00\x00\x01':
            starts.append(i + 4)
            i += 4
        elif data[i:i+3] == b'\x00\x00\x01':
            starts.append(i + 3)
            i += 3
        else:
            i += 1
    return starts


def extract_rbsp(data, nal_start, next_nal_start=None):
    """Extract RBSP bytes, removing emulation prevention bytes."""
    rbsp = []
    end = next_nal_start if next_nal_start else len(data)
    j = nal_start + 1  # skip NAL header byte
    while j < end:
        if j + 2 < end and data[j:j+3] == b'\x00\x00\x03':
            rbsp.append(data[j])
            rbsp.append(data[j+1])
            j += 3
        elif data[j:j+3] == b'\x00\x00\x01' or data[j:j+4] == b'\x00\x00\x00\x01':
            break
        else:
            rbsp.append(data[j])
            j += 1
    return rbsp


class BitReader:
    def __init__(self, bits):
        self.bits = bits
        self.pos = 0

    def read_ue(self):
        lz = 0
        while self.pos + lz < len(self.bits) and self.bits[self.pos + lz] == 0:
            lz += 1
        code = (1 << lz) - 1
        for k in range(lz):
            code += self.bits[self.pos + lz + 1 + k] << (lz - 1 - k)
        self.pos += 2 * lz + 1
        return code

    def read_se(self):
        code = self.read_ue()
        if code == 0:
            return 0
        elif code % 2 == 1:
            return (code + 1) // 2
        else:
            return -(code // 2)

    def read_bits(self, n):
        val = 0
        for _ in range(n):
            val = (val << 1) | self.bits[self.pos]
            self.pos += 1
        return val

    def read_bit(self):
        return self.read_bits(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fixture", required=True, help="Fixture name (without .h264)")
    parser.add_argument("--frame", type=int, required=True, help="Frame index (0-based)")
    parser.add_argument("--frame-num-bits", type=int, default=4,
                        help="Bits for frame_num (log2_max_frame_num_minus4 + 4)")
    args = parser.parse_args()

    h264_path = f"tests/fixtures/{args.fixture}.h264"
    try:
        data = open(h264_path, "rb").read()
    except FileNotFoundError:
        print(f"Error: {h264_path} not found")
        return

    nal_starts = find_nal_starts(data)

    # Find the Nth slice NAL (type 1 or 5)
    slice_count = 0
    for idx, ns in enumerate(nal_starts):
        nal_type = data[ns] & 0x1f
        if nal_type not in (1, 5):
            continue
        if slice_count == args.frame:
            nal_ref_idc = (data[ns] >> 5) & 0x3
            is_idr = (nal_type == 5)
            next_ns = nal_starts[idx + 1] if idx + 1 < len(nal_starts) else None
            rbsp = extract_rbsp(data, ns, next_ns)
            bits = []
            for b in rbsp:
                for bit in range(7, -1, -1):
                    bits.append((b >> bit) & 1)
            br = BitReader(bits)

            print(f"=== Frame {args.frame} ===")
            print(f"NAL type={nal_type} ({'IDR' if is_idr else 'non-IDR'}) "
                  f"ref_idc={nal_ref_idc}")

            first_mb = br.read_ue()
            print(f"  first_mb_in_slice = {first_mb}  (pos={br.pos})")

            slice_type_raw = br.read_ue()
            slice_type = slice_type_raw - 5 if slice_type_raw >= 5 else slice_type_raw
            type_names = {0: "P", 1: "B", 2: "I", 3: "SP", 4: "SI"}
            print(f"  slice_type = {slice_type_raw} ({type_names.get(slice_type, '?')})  (pos={br.pos})")

            pps_id = br.read_ue()
            print(f"  pps_id = {pps_id}  (pos={br.pos})")

            frame_num = br.read_bits(args.frame_num_bits)
            print(f"  frame_num = {frame_num}  ({args.frame_num_bits} bits, pos={br.pos})")

            if is_idr:
                idr_pic_id = br.read_ue()
                print(f"  idr_pic_id = {idr_pic_id}  (pos={br.pos})")

            is_p = slice_type in (0, 3)
            is_b = slice_type == 1
            if is_p or is_b:
                override = br.read_bit()
                print(f"  num_ref_idx_override = {override}  (pos={br.pos})")
                if override:
                    l0 = br.read_ue()
                    print(f"  num_ref_idx_l0_active_minus1 = {l0}  (pos={br.pos})")
                    if is_b:
                        l1 = br.read_ue()
                        print(f"  num_ref_idx_l1_active_minus1 = {l1}  (pos={br.pos})")

            # ref_pic_list_reordering
            if is_p or is_b:
                reorder_l0 = br.read_bit()
                print(f"  reorder_flag_l0 = {reorder_l0}  (pos={br.pos})")
                if reorder_l0:
                    while True:
                        op = br.read_ue()
                        if op == 3:
                            break
                        _ = br.read_ue()
                if is_b:
                    reorder_l1 = br.read_bit()
                    print(f"  reorder_flag_l1 = {reorder_l1}  (pos={br.pos})")

            # dec_ref_pic_marking
            if nal_ref_idc != 0:
                if is_idr:
                    no_output = br.read_bit()
                    long_term = br.read_bit()
                    print(f"  marking: no_output={no_output} long_term={long_term}  (pos={br.pos})")
                else:
                    adaptive = br.read_bit()
                    print(f"  adaptive_marking = {adaptive}  (pos={br.pos})")
                    if adaptive:
                        while True:
                            mmco = br.read_ue()
                            if mmco == 0:
                                break
                            if mmco in (1, 3):
                                _ = br.read_ue()
                            if mmco == 2:
                                _ = br.read_ue()
                            if mmco in (3, 6):
                                _ = br.read_ue()
                            if mmco == 4:
                                _ = br.read_ue()

            qp_delta = br.read_se()
            print(f"  qp_delta = {qp_delta}  (pos={br.pos})")

            deblock = br.read_ue()
            print(f"  deblock_idc = {deblock}  (pos={br.pos})")
            if deblock != 1:
                alpha = br.read_se()
                beta = br.read_se()
                print(f"  alpha={alpha} beta={beta}  (pos={br.pos})")

            print(f"\n  Slice header ends at RBSP bit {br.pos} (NAL bit {br.pos + 8})")
            return

        slice_count += 1

    print(f"Error: frame {args.frame} not found (only {slice_count} slice NALs)")


if __name__ == "__main__":
    main()
