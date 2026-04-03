#!/usr/bin/env python3
"""Manually trace P-frame bitstream skip_run and mb_type values.

Reads raw RBSP bits from the scrolling_texture P-slice NAL and decodes
the slice header + first few skip_run/mb_type pairs to verify alignment.

Usage:
    python scripts/trace_pframe_bits.py
"""
import sys

def read_ue(bits, pos):
    """Read exp-Golomb unsigned from bit array. Returns (value, next_pos)."""
    lz = 0
    while pos + lz < len(bits) and bits[pos + lz] == 0:
        lz += 1
    if pos + lz >= len(bits):
        return None, pos
    code_num = (1 << lz) - 1
    for k in range(lz):
        code_num += bits[pos + lz + 1 + k] << (lz - 1 - k)
    return code_num, pos + 2 * lz + 1

def read_se(bits, pos):
    """Read exp-Golomb signed from bit array."""
    code_num, pos = read_ue(bits, pos)
    if code_num is None:
        return None, pos
    if code_num == 0:
        return 0, pos
    elif code_num % 2 == 1:
        return (code_num + 1) // 2, pos
    else:
        return -(code_num // 2), pos

def read_bits(bits, pos, n):
    """Read n bits as unsigned integer."""
    val = 0
    for i in range(n):
        val = (val << 1) | bits[pos + i]
    return val, pos + n

def main():
    data = open("tests/fixtures/scrolling_texture.h264", "rb").read()

    # Find first non-IDR slice NAL (nal_unit_type=1)
    i = 0
    while i < len(data) - 4:
        if data[i:i+4] == b'\x00\x00\x00\x01':
            nal_type = data[i+4] & 0x1f
            if nal_type == 1:
                nal_start = i + 5  # skip start code + NAL header byte
                break
        i += 1
    else:
        print("No P-slice NAL found")
        return

    # Extract RBSP (remove emulation prevention bytes)
    rbsp = []
    j = nal_start
    while j < len(data) - 2:
        if data[j:j+3] == b'\x00\x00\x03':
            rbsp.append(data[j])
            rbsp.append(data[j+1])
            j += 3
        elif data[j:j+4] == b'\x00\x00\x00\x01' or data[j:j+3] == b'\x00\x00\x01':
            break
        else:
            rbsp.append(data[j])
            j += 1

    # Convert to bits
    bits = []
    for b in rbsp:
        for bit in range(7, -1, -1):
            bits.append((b >> bit) & 1)

    print(f"P-slice RBSP: {len(rbsp)} bytes = {len(bits)} bits")

    # Parse slice header
    pos = 0
    first_mb, pos = read_ue(bits, pos)
    slice_type, pos = read_ue(bits, pos)
    pps_id, pos = read_ue(bits, pos)
    # log2_max_frame_num_minus4 = 0 => bitsInFrameNum = 4
    frame_num, pos = read_bits(bits, pos, 4)
    # num_ref_idx_active_override_flag
    override_flag, pos = read_bits(bits, pos, 1)
    if override_flag:
        num_ref_idx_l0, pos = read_ue(bits, pos)
        num_ref_idx_l0 += 1
    else:
        num_ref_idx_l0 = 1  # PPS default

    # ref_pic_list_reordering_flag_l0
    reorder_flag, pos = read_bits(bits, pos, 1)
    # dec_ref_pic_marking (nal_ref_idc != 0)
    marking_flag, pos = read_bits(bits, pos, 1)
    # slice_qp_delta
    qp_delta, pos = read_se(bits, pos)
    # disable_deblocking_filter_idc
    deblock_idc, pos = read_ue(bits, pos)
    if deblock_idc != 1:
        alpha_offset, pos = read_se(bits, pos)
        beta_offset, pos = read_se(bits, pos)
    else:
        alpha_offset = beta_offset = 0

    print(f"\nSlice header:")
    print(f"  first_mb={first_mb}, type={slice_type}, pps={pps_id}, frame_num={frame_num}")
    print(f"  override={override_flag}, num_ref_l0={num_ref_idx_l0}")
    print(f"  reorder={reorder_flag}, marking={marking_flag}")
    print(f"  qp_delta={qp_delta}, deblock_idc={deblock_idc}")
    print(f"  alpha={alpha_offset}, beta={beta_offset}")
    print(f"  Slice header ends at RBSP bit {pos}")
    print(f"  (NAL bit {pos + 8})")

    # Parse first few skip_run + coded MBs
    print(f"\n=== MB parsing from bit {pos} ===")
    mb_addr = 0
    width_in_mbs = 20
    coded_mb_count = 0

    while mb_addr < 300 and coded_mb_count < 10:
        mbx = mb_addr % width_in_mbs
        mby = mb_addr // width_in_mbs

        # Read skip_run
        skip_start = pos
        skip_run, pos = read_ue(bits, skip_start)
        print(f"\n  skip_run={skip_run} at bit {skip_start} (NAL {skip_start+8})")

        # Process skip MBs
        for s in range(skip_run):
            sx = (mb_addr + s) % width_in_mbs
            sy = (mb_addr + s) // width_in_mbs
            # Skip MBs are not printed (too many)
        mb_addr += skip_run

        if mb_addr >= 300:
            break

        # Read coded MB
        mbx = mb_addr % width_in_mbs
        mby = mb_addr // width_in_mbs
        mb_type_start = pos
        mb_type, pos = read_ue(bits, pos)
        print(f"  MB({mbx},{mby}) mbAddr={mb_addr} mb_type={mb_type} at bit {mb_type_start} (NAL {mb_type_start+8})")

        coded_mb_count += 1
        mb_addr += 1

        # For mb_type >= 5: intra in P-slice, skip detailed parsing
        if mb_type >= 5:
            print(f"    => Intra in P-slice (type {mb_type-5}), skipping residual")
            # Can't easily parse intra residual here — stop
            break

        # For mb_type 0-4: P-inter
        if mb_type <= 2:
            num_parts = 1 if mb_type == 0 else 2
            # ref_idx (only if num_ref_l0 > 1)
            for p in range(num_parts):
                if num_ref_idx_l0 > 1:
                    if num_ref_idx_l0 == 2:
                        _, pos = read_bits(bits, pos, 1)
                    else:
                        _, pos = read_ue(bits, pos)
            # MVD
            for p in range(num_parts):
                mvdx, pos = read_se(bits, pos)
                mvdy, pos = read_se(bits, pos)
                print(f"    part{p} MVD=({mvdx},{mvdy})")
        elif mb_type <= 4:
            # P_8x8 / P_8x8ref0
            sub_types = []
            for s in range(4):
                st, pos = read_ue(bits, pos)
                sub_types.append(st)
            print(f"    sub_mb_types={sub_types}")
            if mb_type == 3:
                for s in range(4):
                    if num_ref_idx_l0 > 1:
                        if num_ref_idx_l0 == 2:
                            _, pos = read_bits(bits, pos, 1)
                        else:
                            _, pos = read_ue(bits, pos)
            for s in range(4):
                num_sub = [1, 2, 2, 4][sub_types[s]]
                for sp in range(num_sub):
                    mvdx, pos = read_se(bits, pos)
                    mvdy, pos = read_se(bits, pos)
                    if sp == 0:
                        print(f"    sub{s} MVD=({mvdx},{mvdy})")

        # CBP
        cbp_code, pos = read_ue(bits, pos)
        # Inter CBP mapping (Table 9-4 column 1)
        inter_cbp = [0,16,32,15,31,47,0,16,32,15,31,47,0,16,32,15,31,47,
                     0,16,32,15,31,47,0,16,32,15,31,47,0,16,32,15,31,47,
                     0,16,32,15,31,47,0,16,32,15,31,47]
        cbp = inter_cbp[cbp_code] if cbp_code < 48 else 0
        cbp_luma = cbp & 0x0f
        cbp_chroma = (cbp >> 4) & 0x03
        print(f"    CBP code={cbp_code} => cbp=0x{cbp:02x} (luma={cbp_luma}, chroma={cbp_chroma})")

        if cbp > 0:
            qp_d, pos = read_se(bits, pos)
            print(f"    qp_delta={qp_d}")

        # Skip residual parsing (too complex for a simple script)
        print(f"    Residual starts at bit {pos} (NAL {pos+8})")
        print(f"    *** Cannot parse CAVLC residual in Python — stopping ***")
        break

if __name__ == "__main__":
    main()
