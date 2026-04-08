#!/usr/bin/env python3
"""Parse SPS from cabac_flat_main.h264 to determine POC type and
delta_pic_order_always_zero_flag, then compute exact slice header bit count."""
import sys

class BitReader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def read_bit(self):
        byte_idx = self.pos >> 3
        bit_idx = 7 - (self.pos & 7)
        self.pos += 1
        return (self.data[byte_idx] >> bit_idx) & 1

    def read_bits(self, n):
        val = 0
        for _ in range(n):
            val = (val << 1) | self.read_bit()
        return val

    def read_ue(self):
        zeros = 0
        while self.read_bit() == 0:
            zeros += 1
        if zeros == 0:
            return 0
        suffix = self.read_bits(zeros)
        return (1 << zeros) - 1 + suffix

    def read_se(self):
        val = self.read_ue()
        if val == 0:
            return 0
        if val & 1:
            return (val + 1) // 2
        return -(val // 2)


def remove_epb(data):
    """Remove emulation prevention bytes."""
    out = bytearray()
    i = 0
    while i < len(data):
        if i >= 2 and data[i-2] == 0 and data[i-1] == 0 and data[i] == 3:
            i += 1
            continue
        out.append(data[i])
        i += 1
    return bytes(out)


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

    sps_data = pps_data = idr_data = None
    for nal_off in nals:
        nal_type = data[nal_off] & 0x1F
        # Find next start code for NAL size
        end = len(data)
        for next_off in nals:
            if next_off > nal_off + 1:
                # back up past start code
                e = next_off - 3
                if e > 0 and data[e-1] == 0:
                    e -= 1
                end = e
                break
        nal_bytes = data[nal_off+1:end]  # skip NAL header
        if nal_type == 7:
            sps_data = remove_epb(nal_bytes)
        elif nal_type == 8:
            pps_data = remove_epb(nal_bytes)
        elif nal_type == 5:
            idr_data = remove_epb(nal_bytes)

    # Parse SPS
    print("=== SPS ===")
    br = BitReader(sps_data)
    profile_idc = br.read_bits(8)
    constraint_set_flags = br.read_bits(8)
    level_idc = br.read_bits(8)
    sps_id = br.read_ue()
    print(f"  profile_idc={profile_idc}, level_idc={level_idc}, sps_id={sps_id}")

    if profile_idc in (100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134):
        chroma_format_idc = br.read_ue()
        if chroma_format_idc == 3:
            br.read_bit()  # separate_colour_plane
        br.read_ue()  # bit_depth_luma_minus8
        br.read_ue()  # bit_depth_chroma_minus8
        br.read_bit()  # qpprime_y_zero_transform_bypass
        scaling_present = br.read_bit()
        if scaling_present:
            pass  # skip scaling lists
    else:
        chroma_format_idc = 1  # default

    log2_max_frame_num_minus4 = br.read_ue()
    bits_in_frame_num = log2_max_frame_num_minus4 + 4
    print(f"  log2_max_frame_num_minus4={log2_max_frame_num_minus4} -> bits_in_frame_num={bits_in_frame_num}")

    poc_type = br.read_ue()
    print(f"  pic_order_cnt_type={poc_type}")

    delta_pic_order_always_zero = 0
    if poc_type == 0:
        log2_max_poc_lsb_m4 = br.read_ue()
        poc_lsb_bits = log2_max_poc_lsb_m4 + 4
        print(f"  log2_max_pic_order_cnt_lsb_minus4={log2_max_poc_lsb_m4}")
    elif poc_type == 1:
        delta_pic_order_always_zero = br.read_bit()
        offset_non_ref = br.read_se()
        offset_top_bottom = br.read_se()
        num_ref_in_cycle = br.read_ue()
        print(f"  delta_pic_order_always_zero_flag={delta_pic_order_always_zero}")
        print(f"  offset_for_non_ref_pic={offset_non_ref}")
        print(f"  offset_for_top_to_bottom_field={offset_top_bottom}")
        print(f"  num_ref_frames_in_pic_order_cnt_cycle={num_ref_in_cycle}")
        for i in range(num_ref_in_cycle):
            off = br.read_se()
            print(f"    offset_for_ref_frame[{i}]={off}")

    max_num_ref_frames = br.read_ue()
    gaps_allowed = br.read_bit()
    pic_width_minus1 = br.read_ue()
    pic_height_minus1 = br.read_ue()
    frame_mbs_only = br.read_bit()
    print(f"  max_num_ref_frames={max_num_ref_frames}")
    print(f"  frame_mbs_only_flag={frame_mbs_only}")
    print(f"  pic_width_in_mbs_minus1={pic_width_minus1} -> {(pic_width_minus1+1)*16}")
    print(f"  pic_height_in_map_units_minus1={pic_height_minus1} -> {(pic_height_minus1+1)*16}")

    # Parse PPS
    print("\n=== PPS ===")
    br2 = BitReader(pps_data)
    pps_id = br2.read_ue()
    sps_id_ref = br2.read_ue()
    entropy_coding_mode = br2.read_bit()
    pic_order_present = br2.read_bit()
    print(f"  pps_id={pps_id}, sps_id={sps_id_ref}")
    print(f"  entropy_coding_mode_flag={entropy_coding_mode} ({'CABAC' if entropy_coding_mode else 'CAVLC'})")
    print(f"  bottom_field_pic_order_in_frame_present_flag={pic_order_present}")
    num_slice_groups_minus1 = br2.read_ue()
    print(f"  num_slice_groups_minus1={num_slice_groups_minus1}")
    num_ref_idx_l0_active_minus1 = br2.read_ue()
    num_ref_idx_l1_active_minus1 = br2.read_ue()
    weighted_pred = br2.read_bit()
    weighted_bipred_idc = br2.read_bits(2)
    pic_init_qp_minus26 = br2.read_se()
    pic_init_qs_minus26 = br2.read_se()
    chroma_qp_index_offset = br2.read_se()
    deblocking_filter_present = br2.read_bit()
    constrained_intra = br2.read_bit()
    redundant_pic_cnt_present = br2.read_bit()
    print(f"  weighted_pred_flag={weighted_pred}")
    print(f"  pic_init_qp_minus26={pic_init_qp_minus26}")
    print(f"  chroma_qp_index_offset={chroma_qp_index_offset}")
    print(f"  deblocking_filter_control_present_flag={deblocking_filter_present}")
    print(f"  redundant_pic_cnt_present_flag={redundant_pic_cnt_present}")

    # Now parse slice header with exact bit counting
    print("\n=== Slice Header ===")
    br3 = BitReader(idr_data)
    pos_before = br3.pos

    first_mb = br3.read_ue()
    print(f"  first_mb_in_slice={first_mb} (bits {pos_before}-{br3.pos-1}, pos now {br3.pos})")

    slice_type_raw = br3.read_ue()
    print(f"  slice_type={slice_type_raw} (pos now {br3.pos})")

    sh_pps_id = br3.read_ue()
    print(f"  pps_id={sh_pps_id} (pos now {br3.pos})")

    frame_num_val = br3.read_bits(bits_in_frame_num)
    print(f"  frame_num={frame_num_val} ({bits_in_frame_num} bits, pos now {br3.pos})")

    if not frame_mbs_only:
        field_pic = br3.read_bit()
        print(f"  field_pic_flag={field_pic}")
        if field_pic:
            br3.read_bit()  # bottom_field_flag

    # IDR
    idr_pic_id = br3.read_ue()
    print(f"  idr_pic_id={idr_pic_id} (pos now {br3.pos})")

    # POC
    if poc_type == 0:
        poc_lsb = br3.read_bits(poc_lsb_bits)
        print(f"  pic_order_cnt_lsb={poc_lsb} (pos now {br3.pos})")
        if pic_order_present:
            delta_bottom = br3.read_se()
            print(f"  delta_pic_order_cnt_bottom={delta_bottom}")

    if poc_type == 1 and not delta_pic_order_always_zero:
        delta_poc_0 = br3.read_se()
        print(f"  delta_pic_order_cnt[0]={delta_poc_0} (pos now {br3.pos})")
        if pic_order_present:
            delta_poc_1 = br3.read_se()
            print(f"  delta_pic_order_cnt[1]={delta_poc_1} (pos now {br3.pos})")

    # redundant_pic_cnt
    if redundant_pic_cnt_present:
        rpc = br3.read_ue()
        print(f"  redundant_pic_cnt={rpc}")

    # num_ref_idx_active_override_flag - NOT for I-slices
    # (P/SP/B only)

    # ref_pic_list_modification - NOT for I-slices

    # dec_ref_pic_marking (nal_ref_idc != 0, which is true for IDR)
    # For IDR:
    no_output = br3.read_bit()
    long_term = br3.read_bit()
    print(f"  no_output_of_prior_pics_flag={no_output} (pos now {br3.pos})")
    print(f"  long_term_reference_flag={long_term} (pos now {br3.pos})")

    # cabac_init_idc - NOT for I-slices

    # slice_qp_delta
    qp_delta = br3.read_se()
    print(f"  slice_qp_delta={qp_delta} (pos now {br3.pos})")

    # deblocking_filter_control
    if deblocking_filter_present:
        disable_deblocking = br3.read_ue()
        print(f"  disable_deblocking_filter_idc={disable_deblocking} (pos now {br3.pos})")
        if disable_deblocking != 1:
            alpha = br3.read_se()
            beta = br3.read_se()
            print(f"  slice_alpha_c0_offset_div2={alpha} (pos now {br3.pos})")
            print(f"  slice_beta_offset_div2={beta} (pos now {br3.pos})")

    # CABAC alignment
    align_bits = (8 - (br3.pos % 8)) % 8
    print(f"\n  Slice header end at bit {br3.pos}")
    print(f"  Alignment padding: {align_bits} bits")
    print(f"  CABAC data starts at bit {br3.pos + align_bits}")
    print(f"  CABAC data starts at byte {(br3.pos + align_bits) // 8}")

    # Verify by reading CABAC init
    br3.pos = br3.pos + align_bits  # align
    cabac_start_bit = br3.pos
    cabac_init = br3.read_bits(9)
    print(f"\n  CABAC init 9 bits from bit {cabac_start_bit}: offset={cabac_init}")
    print(f"  Expected: 509 (0x1FD)")


if __name__ == "__main__":
    main()
