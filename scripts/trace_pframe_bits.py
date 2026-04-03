#!/usr/bin/env python3
"""Parse P-frame bitstream: slice header, skip_runs, mb_type, ref_idx, MVD.

Reads raw RBSP bits and decodes syntax elements to verify bit alignment.

Usage:
    python scripts/trace_pframe_bits.py --fixture pan_up --frame 16
    python scripts/trace_pframe_bits.py --fixture scrolling_texture --frame 1 --mbs 5
"""
import argparse
import sys
sys.path.insert(0, "scripts")
from trace_slice_header_bits import find_nal_starts, extract_rbsp, BitReader


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fixture", default="scrolling_texture",
                        help="Fixture name (without .h264)")
    parser.add_argument("--frame", type=int, default=1,
                        help="Frame index (0-based)")
    parser.add_argument("--frame-num-bits", type=int, default=4,
                        help="Bits for frame_num (log2_max_frame_num_minus4 + 4)")
    parser.add_argument("--mbs", type=int, default=3,
                        help="Number of coded MBs to parse")
    parser.add_argument("--pps-num-ref-l0", type=int, default=3,
                        help="PPS default num_ref_idx_l0_active (minus1 + 1)")
    args = parser.parse_args()

    h264_path = f"tests/fixtures/{args.fixture}.h264"
    try:
        data = open(h264_path, "rb").read()
    except FileNotFoundError:
        print(f"Error: {h264_path} not found")
        return

    nal_starts = find_nal_starts(data)

    # Find the target frame's slice NAL
    slice_count = 0
    for idx, ns in enumerate(nal_starts):
        nal_type = data[ns] & 0x1f
        if nal_type not in (1, 5):
            continue
        if slice_count == args.frame:
            is_idr = (nal_type == 5)
            nal_ref_idc = (data[ns] >> 5) & 0x3
            next_ns = nal_starts[idx + 1] if idx + 1 < len(nal_starts) else None
            rbsp = extract_rbsp(data, ns, next_ns)
            bits = []
            for b in rbsp:
                for bit in range(7, -1, -1):
                    bits.append((b >> bit) & 1)
            br = BitReader(bits)

            print(f"=== Frame {args.frame} of {args.fixture} ===")
            print(f"NAL type={nal_type} ref_idc={nal_ref_idc}\n")

            # Parse slice header
            first_mb = br.read_ue()
            slice_type_raw = br.read_ue()
            slice_type = slice_type_raw - 5 if slice_type_raw >= 5 else slice_type_raw
            pps_id = br.read_ue()
            frame_num = br.read_bits(args.frame_num_bits)
            print(f"Slice header: first_mb={first_mb} type={slice_type_raw}"
                  f" pps={pps_id} fn={frame_num}")

            if is_idr:
                idr_pic_id = br.read_ue()
                print(f"  idr_pic_id={idr_pic_id}")

            is_p = slice_type in (0, 3)
            num_ref_l0 = args.pps_num_ref_l0

            if is_p:
                override = br.read_bit()
                if override:
                    num_ref_l0 = br.read_ue() + 1
                print(f"  override={override} num_ref_l0={num_ref_l0}")

                reorder = br.read_bit()
                print(f"  reorder_flag={reorder}")
                if reorder:
                    while True:
                        op = br.read_ue()
                        if op == 3:
                            break
                        _ = br.read_ue()

            if nal_ref_idc != 0:
                if is_idr:
                    br.read_bit()  # no_output
                    br.read_bit()  # long_term
                else:
                    adaptive = br.read_bit()
                    print(f"  adaptive_marking={adaptive}")

            qp_delta = br.read_se()
            deblock = br.read_ue()
            alpha = beta = 0
            if deblock != 1:
                alpha = br.read_se()
                beta = br.read_se()
            print(f"  qp_delta={qp_delta} deblock={deblock} alpha={alpha} beta={beta}")
            print(f"  Header ends at RBSP bit {br.pos} (NAL {br.pos+8})")

            # Parse MB-level data
            print(f"\n=== MB parsing ===")
            mb_addr = 0
            width_in_mbs = 20  # 320/16
            coded_count = 0

            while mb_addr < 300 and coded_count < args.mbs:
                skip_run = br.read_ue()
                mbx = mb_addr % width_in_mbs
                mby = mb_addr // width_in_mbs
                if skip_run > 0:
                    print(f"\n  skip_run={skip_run} at bit {br.pos}"
                          f" (MB({mbx},{mby}) to MB({(mb_addr+skip_run-1)%width_in_mbs},"
                          f"{(mb_addr+skip_run-1)//width_in_mbs}))")
                mb_addr += skip_run
                if mb_addr >= 300:
                    break

                mbx = mb_addr % width_in_mbs
                mby = mb_addr // width_in_mbs
                mb_type = br.read_ue()
                print(f"\n  MB({mbx},{mby}) addr={mb_addr} type={mb_type} at bit {br.pos}")

                if mb_type >= 5:
                    print(f"    Intra in P-slice (type {mb_type-5}), stopping")
                    break

                coded_count += 1
                mb_addr += 1

                if mb_type <= 2:
                    num_parts = 1 if mb_type == 0 else 2
                    names = {0: "16x16", 1: "16x8", 2: "8x16"}
                    print(f"    {names[mb_type]}: {num_parts} partitions")

                    # ref_idx
                    for p in range(num_parts):
                        if num_ref_l0 > 1:
                            if num_ref_l0 == 2:
                                ri = 1 - br.read_bit()
                            else:
                                ri = br.read_ue()
                            print(f"    ref_idx[{p}]={ri} (pos={br.pos})")
                        else:
                            print(f"    ref_idx[{p}]=0 (inferred)")

                    # MVD
                    for p in range(num_parts):
                        mvdx = br.read_se()
                        mvdy = br.read_se()
                        print(f"    MVD[{p}]=({mvdx},{mvdy}) (pos={br.pos})")

                elif mb_type <= 4:
                    print(f"    P_8x8{'ref0' if mb_type==4 else ''}")
                    sub_types = [br.read_ue() for _ in range(4)]
                    print(f"    sub_types={sub_types}")
                    if mb_type == 3:
                        for s in range(4):
                            if num_ref_l0 > 1:
                                if num_ref_l0 == 2:
                                    ri = 1 - br.read_bit()
                                else:
                                    ri = br.read_ue()
                                print(f"    sub_ref[{s}]={ri}")
                    for s in range(4):
                        num_sub = [1, 2, 2, 4][sub_types[s]]
                        for sp in range(num_sub):
                            mvdx = br.read_se()
                            mvdy = br.read_se()
                            if sp == 0:
                                print(f"    sub{s} MVD=({mvdx},{mvdy})")

                # CBP
                cbp_code = br.read_ue()
                inter_cbp = [0,16,32,15,31,47,0,16,32,15,31,47,
                             0,16,32,15,31,47,0,16,32,15,31,47,
                             0,16,32,15,31,47,0,16,32,15,31,47,
                             0,16,32,15,31,47,0,16,32,15,31,47]
                cbp = inter_cbp[cbp_code] if cbp_code < 48 else 0
                print(f"    CBP code={cbp_code} cbp=0x{cbp:02x} (pos={br.pos})")
                if cbp > 0:
                    qpd = br.read_se()
                    print(f"    qp_delta={qpd}")
                    print(f"    Residual at bit {br.pos} (cannot parse in Python)")
                    break

            return

        slice_count += 1

    print(f"Error: frame {args.frame} not found (only {slice_count} slices)")


if __name__ == "__main__":
    main()
