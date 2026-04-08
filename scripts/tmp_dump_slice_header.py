#!/usr/bin/env python3
"""Dump the raw bytes of the IDR NAL and parse slice header bits manually."""
import sys

def read_ue(bits, pos):
    """Read exp-Golomb unsigned from bit array starting at pos."""
    zeros = 0
    while pos < len(bits) and bits[pos] == 0:
        zeros += 1
        pos += 1
    pos += 1  # skip the 1 bit
    val = 0
    for i in range(zeros):
        val = (val << 1) | bits[pos]
        pos += 1
    return val, pos

def read_se(bits, pos):
    val, pos = read_ue(bits, pos)
    if val & 1:
        return (val + 1) // 2, pos
    else:
        return -(val // 2), pos

def main():
    h264_file = sys.argv[1] if len(sys.argv) > 1 else "tests/fixtures/cabac_flat_main.h264"
    data = open(h264_file, "rb").read()

    # Find NALs
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

    # Find SPS, PPS, and IDR
    sps_off = pps_off = idr_off = None
    for nal_off in nals:
        nal_type = data[nal_off] & 0x1F
        if nal_type == 7:
            sps_off = nal_off
        elif nal_type == 8:
            pps_off = nal_off
        elif nal_type == 5:
            idr_off = nal_off

    # Dump SPS key fields
    if sps_off:
        print(f"SPS NAL at offset {sps_off}, type={data[sps_off] & 0x1F}")
        print(f"  profile_idc={data[sps_off+1]}, level_idc={data[sps_off+3]}")

    # Dump raw IDR bytes
    if idr_off:
        print(f"\nIDR NAL header at offset {idr_off}: 0x{data[idr_off]:02x}")
        print(f"  nal_ref_idc = {(data[idr_off] >> 5) & 3}")
        print(f"  nal_unit_type = {data[idr_off] & 0x1F}")

        # Convert to RBSP (remove emulation prevention bytes)
        raw = data[idr_off + 1:]  # skip NAL header
        rbsp = bytearray()
        j = 0
        epb_count = 0
        epb_positions = []
        while j < len(raw):
            if j >= 2 and raw[j-2] == 0 and raw[j-1] == 0 and raw[j] == 3:
                epb_count += 1
                epb_positions.append(j)
                j += 1  # skip the 0x03
                continue
            rbsp.append(raw[j])
            j += 1

        print(f"\n  Raw bytes (first 20): {' '.join(f'{b:02x}' for b in raw[:20])}")
        print(f"  RBSP bytes (first 20): {' '.join(f'{b:02x}' for b in rbsp[:20])}")
        print(f"  Emulation prevention bytes: {epb_count} at positions {epb_positions[:10]}")

        # Convert RBSP to bit array
        bits = []
        for b in rbsp:
            for bit_i in range(7, -1, -1):
                bits.append((b >> bit_i) & 1)

        # Parse slice header manually
        pos = 0
        first_mb, pos = read_ue(bits, pos)
        print(f"\n  Slice header fields:")
        print(f"    first_mb_in_slice = {first_mb} (bits 0-{pos-1})")

        slice_type, pos = read_ue(bits, pos)
        print(f"    slice_type = {slice_type} (through bit {pos-1})")

        pps_id, pos = read_ue(bits, pos)
        print(f"    pic_parameter_set_id = {pps_id} (through bit {pos-1})")

        # frame_num: u(log2_max_frame_num_minus4 + 4) bits
        # Need SPS to know the exact size. Try common values.
        # For simplicity, assume log2_max_frame_num_minus4 = 0 -> 4 bits
        print(f"    frame_num bits starting at bit {pos}")
        # Try parsing SPS for log2_max_frame_num
        if sps_off:
            sps_raw = data[sps_off + 1:]
            sps_rbsp = bytearray()
            j = 0
            while j < len(sps_raw) and j < 50:
                if j >= 2 and sps_raw[j-2] == 0 and sps_raw[j-1] == 0 and sps_raw[j] == 3:
                    j += 1
                    continue
                sps_rbsp.append(sps_raw[j])
                j += 1
            sps_bits = []
            for b in sps_rbsp:
                for bit_i in range(7, -1, -1):
                    sps_bits.append((b >> bit_i) & 1)
            # Profile, constraint flags, level = 3 bytes = 24 bits
            sps_pos = 24
            sps_id, sps_pos = read_ue(sps_bits, sps_pos)
            print(f"    (SPS: sps_id={sps_id})")
            profile = data[sps_off + 1]
            if profile in (100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134):
                # High profile: skip chroma_format_idc etc.
                chroma_fmt, sps_pos = read_ue(sps_bits, sps_pos)
                print(f"    (SPS: chroma_format_idc={chroma_fmt})")
                if chroma_fmt == 3:
                    sps_pos += 1  # separate_colour_plane_flag
                bit_depth_luma, sps_pos = read_ue(sps_bits, sps_pos)
                bit_depth_chroma, sps_pos = read_ue(sps_bits, sps_pos)
                print(f"    (SPS: bit_depth_luma_minus8={bit_depth_luma}, chroma={bit_depth_chroma})")
                sps_pos += 1  # qpprime_y_zero_transform_bypass
                # scaling_matrix_present_flag
                scaling_present = sps_bits[sps_pos]
                sps_pos += 1
                print(f"    (SPS: scaling_matrix_present={scaling_present})")
                # Skip scaling lists if present...

            log2_max_frame_num_m4, sps_pos = read_ue(sps_bits, sps_pos)
            print(f"    (SPS: log2_max_frame_num_minus4={log2_max_frame_num_m4})")
            frame_num_bits = log2_max_frame_num_m4 + 4

            poc_type, sps_pos = read_ue(sps_bits, sps_pos)
            print(f"    (SPS: pic_order_cnt_type={poc_type})")
            if poc_type == 0:
                log2_max_poc_m4, sps_pos = read_ue(sps_bits, sps_pos)
                print(f"    (SPS: log2_max_pic_order_cnt_lsb_minus4={log2_max_poc_m4})")
                poc_lsb_bits = log2_max_poc_m4 + 4
            else:
                poc_lsb_bits = 0

        else:
            frame_num_bits = 4
            poc_lsb_bits = 4

        frame_num_val = 0
        for b in range(frame_num_bits):
            frame_num_val = (frame_num_val << 1) | bits[pos]
            pos += 1
        print(f"    frame_num = {frame_num_val} ({frame_num_bits} bits, through bit {pos-1})")

        # IDR: idr_pic_id
        idr_pic_id, pos = read_ue(bits, pos)
        print(f"    idr_pic_id = {idr_pic_id} (through bit {pos-1})")

        # POC (if poc_type == 0)
        if poc_type == 0:
            poc_lsb = 0
            for b in range(poc_lsb_bits):
                poc_lsb = (poc_lsb << 1) | bits[pos]
                pos += 1
            print(f"    pic_order_cnt_lsb = {poc_lsb} ({poc_lsb_bits} bits, through bit {pos-1})")

        # dec_ref_pic_marking (for IDR)
        no_output = bits[pos]
        pos += 1
        print(f"    no_output_of_prior_pics_flag = {no_output} (bit {pos-1})")
        long_term = bits[pos]
        pos += 1
        print(f"    long_term_reference_flag = {long_term} (bit {pos-1})")

        # slice_qp_delta
        qp_delta, pos = read_ue(bits, pos)  # Actually se(v)
        # Re-parse as se(v)
        pos_save = pos
        # Go back and parse as se
        pos_back = pos_save - 1  # rough, let me just reparse
        # Actually let me just re-read from where qp_delta started
        print(f"    (need to re-parse qp_delta...)")

        # Actually, let me just count bits up to alignment
        # Before slice_qp_delta, there might be cabac_init_idc for non-I slices
        # For I-slice: no cabac_init_idc
        # Next is slice_qp_delta (se)

        # Let me restart the QP delta parse
        qp_delta_start = pos - (qp_delta.bit_length() if hasattr(qp_delta, 'bit_length') else 1)
        # This is getting messy; let me just print current position
        print(f"    Current bit position after POC marking: {pos - 2}")  # before last 2 reads

        # Let me just track total header bits including alignment
        # For CABAC: cabac_alignment_one_bit follows
        # After slice_qp_delta, next is:
        # deblocking_filter_control_present_flag (from PPS) determines if
        # disable_deblocking_filter_idc is present

        print(f"\n  Total slice header bits (before CABAC alignment): ~{pos}")
        # CABAC alignment: skip bits until byte-aligned
        align_bits = (8 - (pos % 8)) % 8
        if align_bits == 0:
            align_bits = 0  # already aligned
        print(f"  CABAC alignment: {align_bits} bits to next byte boundary")
        print(f"  CABAC data starts at RBSP bit: {pos + align_bits}")
        print(f"  CABAC data starts at RBSP byte: {(pos + align_bits) // 8}")

        # Also print raw bits around the boundary
        print(f"\n  RBSP bits 0-{min(48, len(bits))-1}:")
        for row_start in range(0, min(48, len(bits)), 8):
            row_bits = bits[row_start:row_start+8]
            byte_val = 0
            for b in row_bits:
                byte_val = (byte_val << 1) | b
            print(f"    bits {row_start:2d}-{row_start+7:2d}: {''.join(str(b) for b in row_bits)} = 0x{byte_val:02x}")


if __name__ == "__main__":
    main()
