#!/usr/bin/env python3
"""Parse SPS and PPS NAL units from H.264 bitstreams to compare encoding parameters."""
import sys
import struct


class BitReader:
    def __init__(self, data):
        self.data = data
        self.bit_pos = 0

    def read_bits(self, n):
        val = 0
        for _ in range(n):
            byte_idx = self.bit_pos // 8
            bit_idx = 7 - (self.bit_pos % 8)
            if byte_idx < len(self.data):
                val = (val << 1) | ((self.data[byte_idx] >> bit_idx) & 1)
            self.bit_pos += 1
        return val

    def read_ue(self):
        """Exp-Golomb unsigned."""
        leading_zeros = 0
        while self.read_bits(1) == 0:
            leading_zeros += 1
            if leading_zeros > 32:
                return -1
        return (1 << leading_zeros) - 1 + self.read_bits(leading_zeros)

    def read_se(self):
        """Exp-Golomb signed."""
        val = self.read_ue()
        if val % 2 == 0:
            return -(val // 2)
        else:
            return (val + 1) // 2

    def read_flag(self):
        return self.read_bits(1)


def find_nals(data, max_offset=5000):
    """Find NAL units in Annex B format."""
    nals = []
    i = 0
    while i < min(len(data), max_offset):
        if data[i:i+4] == b'\x00\x00\x00\x01':
            start = i + 4
            nal_type = data[start] & 0x1f
            nal_ref_idc = (data[start] >> 5) & 0x3
            # Find end
            end = len(data)
            j = start + 1
            while j < min(len(data), max_offset):
                if data[j:j+3] == b'\x00\x00\x01' or data[j:j+4] == b'\x00\x00\x00\x01':
                    end = j
                    break
                j += 1
            nals.append((nal_type, nal_ref_idc, data[start+1:end]))
            i = start + 1
        elif data[i:i+3] == b'\x00\x00\x01':
            start = i + 3
            nal_type = data[start] & 0x1f
            nal_ref_idc = (data[start] >> 5) & 0x3
            end = len(data)
            j = start + 1
            while j < min(len(data), max_offset):
                if data[j:j+3] == b'\x00\x00\x01' or data[j:j+4] == b'\x00\x00\x00\x01':
                    end = j
                    break
                j += 1
            nals.append((nal_type, nal_ref_idc, data[start+1:end]))
            i = start + 1
        else:
            i += 1
    return nals


def parse_sps(data):
    """Parse SPS (ITU-T H.264 section 7.3.2.1)."""
    br = BitReader(data)
    profile_idc = br.read_bits(8)
    constraint_set0 = br.read_flag()
    constraint_set1 = br.read_flag()
    constraint_set2 = br.read_flag()
    constraint_set3 = br.read_flag()
    constraint_set4 = br.read_flag()
    constraint_set5 = br.read_flag()
    reserved = br.read_bits(2)
    level_idc = br.read_bits(8)
    sps_id = br.read_ue()

    print(f"  profile_idc: {profile_idc} ({'Baseline' if profile_idc==66 else 'Main' if profile_idc==77 else 'High' if profile_idc==100 else '?'})")
    print(f"  constraint_set flags: {constraint_set0}{constraint_set1}{constraint_set2}{constraint_set3}{constraint_set4}{constraint_set5}")
    print(f"  level_idc: {level_idc} (Level {level_idc/10:.1f})")
    print(f"  sps_id: {sps_id}")

    if profile_idc in (100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134):
        chroma_format_idc = br.read_ue()
        print(f"  chroma_format_idc: {chroma_format_idc}")
        if chroma_format_idc == 3:
            br.read_flag()  # separate_colour_plane_flag
        bit_depth_luma = br.read_ue() + 8
        bit_depth_chroma = br.read_ue() + 8
        print(f"  bit_depth_luma: {bit_depth_luma}")
        print(f"  bit_depth_chroma: {bit_depth_chroma}")
        br.read_flag()  # qpprime_y_zero_transform_bypass
        seq_scaling_matrix_present = br.read_flag()
        print(f"  seq_scaling_matrix_present: {seq_scaling_matrix_present}")
        if seq_scaling_matrix_present:
            # Skip scaling lists
            for i in range(8 if chroma_format_idc != 3 else 12):
                if br.read_flag():  # seq_scaling_list_present_flag
                    size = 16 if i < 6 else 64
                    for _ in range(size):
                        br.read_se()

    log2_max_frame_num = br.read_ue() + 4
    pic_order_cnt_type = br.read_ue()
    print(f"  log2_max_frame_num: {log2_max_frame_num}")
    print(f"  pic_order_cnt_type: {pic_order_cnt_type}")

    if pic_order_cnt_type == 0:
        log2_max_poc_lsb = br.read_ue() + 4
        print(f"  log2_max_pic_order_cnt_lsb: {log2_max_poc_lsb}")
    elif pic_order_cnt_type == 1:
        br.read_flag()
        br.read_se()
        br.read_se()
        n = br.read_ue()
        for _ in range(n):
            br.read_se()

    max_num_ref_frames = br.read_ue()
    gaps_allowed = br.read_flag()
    pic_width_in_mbs = br.read_ue() + 1
    pic_height_in_map_units = br.read_ue() + 1
    frame_mbs_only = br.read_flag()

    print(f"  max_num_ref_frames: {max_num_ref_frames}")
    print(f"  gaps_in_frame_num_allowed: {gaps_allowed}")
    print(f"  pic_width_in_mbs: {pic_width_in_mbs} ({pic_width_in_mbs*16}px)")
    print(f"  pic_height_in_map_units: {pic_height_in_map_units} ({pic_height_in_map_units*16}px)")
    print(f"  frame_mbs_only: {frame_mbs_only}")

    if not frame_mbs_only:
        br.read_flag()  # mb_adaptive_frame_field

    br.read_flag()  # direct_8x8_inference

    frame_cropping = br.read_flag()
    print(f"  frame_cropping: {frame_cropping}")
    if frame_cropping:
        crop_left = br.read_ue()
        crop_right = br.read_ue()
        crop_top = br.read_ue()
        crop_bottom = br.read_ue()
        print(f"  crop: left={crop_left} right={crop_right} top={crop_top} bottom={crop_bottom}")

    vui_present = br.read_flag()
    print(f"  vui_present: {vui_present}")


def parse_pps(data):
    """Parse PPS (ITU-T H.264 section 7.3.2.2)."""
    br = BitReader(data)
    pps_id = br.read_ue()
    sps_id = br.read_ue()
    entropy_coding_mode = br.read_flag()
    bottom_field_pic_order = br.read_flag()
    num_slice_groups = br.read_ue() + 1

    print(f"  pps_id: {pps_id}")
    print(f"  sps_id: {sps_id}")
    print(f"  entropy_coding_mode: {entropy_coding_mode} ({'CABAC' if entropy_coding_mode else 'CAVLC'})")
    print(f"  bottom_field_pic_order_in_frame_present: {bottom_field_pic_order}")
    print(f"  num_slice_groups: {num_slice_groups}")

    if num_slice_groups > 1:
        print("  WARNING: multiple slice groups (FMO), skipping detailed parse")
        return

    num_ref_idx_l0_default_active = br.read_ue() + 1
    num_ref_idx_l1_default_active = br.read_ue() + 1
    weighted_pred = br.read_flag()
    weighted_bipred_idc = br.read_bits(2)
    pic_init_qp = br.read_se() + 26
    pic_init_qs = br.read_se() + 26
    chroma_qp_index_offset = br.read_se()
    deblocking_filter_control_present = br.read_flag()
    constrained_intra_pred = br.read_flag()
    redundant_pic_cnt_present = br.read_flag()

    print(f"  num_ref_idx_l0_default_active: {num_ref_idx_l0_default_active}")
    print(f"  num_ref_idx_l1_default_active: {num_ref_idx_l1_default_active}")
    print(f"  weighted_pred_flag: {weighted_pred}")
    print(f"  weighted_bipred_idc: {weighted_bipred_idc}")
    print(f"  pic_init_qp: {pic_init_qp}")
    print(f"  pic_init_qs: {pic_init_qs}")
    print(f"  chroma_qp_index_offset: {chroma_qp_index_offset}")
    print(f"  deblocking_filter_control_present: {deblocking_filter_control_present}")
    print(f"  constrained_intra_pred: {constrained_intra_pred}")
    print(f"  redundant_pic_cnt_present: {redundant_pic_cnt_present}")


def parse_slice_header_partial(data, nal_type):
    """Parse beginning of slice header to get QP delta."""
    br = BitReader(data)
    first_mb_in_slice = br.read_ue()
    slice_type = br.read_ue()
    pps_id = br.read_ue()
    frame_num = br.read_bits(4)  # Assuming log2_max_frame_num=4

    slice_type_names = {0: 'P', 1: 'B', 2: 'I', 3: 'SP', 4: 'SI', 5: 'P', 6: 'B', 7: 'I', 8: 'SP', 9: 'SI'}
    print(f"  first_mb_in_slice: {first_mb_in_slice}")
    print(f"  slice_type: {slice_type} ({slice_type_names.get(slice_type, '?')})")
    print(f"  pps_id: {pps_id}")
    print(f"  frame_num: {frame_num}")

    if nal_type == 5:  # IDR
        idr_pic_id = br.read_ue()
        print(f"  idr_pic_id: {idr_pic_id}")

    # pic_order_cnt_lsb (assuming poc_type=0, log2_max_poc_lsb=4+something)
    poc_lsb = br.read_bits(4)  # Assuming log2_max_pic_order_cnt_lsb=4
    print(f"  pic_order_cnt_lsb: {poc_lsb}")


def analyze_file(filepath):
    print(f"\n{'='*60}")
    print(f"Analyzing: {filepath}")
    print(f"{'='*60}")

    with open(filepath, 'rb') as f:
        data = f.read(5000)

    nals = find_nals(data)
    nal_type_names = {1: 'Slice', 5: 'IDR', 6: 'SEI', 7: 'SPS', 8: 'PPS'}

    for nal_type, nal_ref_idc, nal_data in nals:
        name = nal_type_names.get(nal_type, f'type_{nal_type}')
        print(f"\n--- NAL {name} (type={nal_type}, ref_idc={nal_ref_idc}, {len(nal_data)} bytes) ---")
        print(f"  raw hex (first 30 bytes): {nal_data[:30].hex()}")

        if nal_type == 7:
            parse_sps(nal_data)
        elif nal_type == 8:
            parse_pps(nal_data)
        elif nal_type == 5:
            parse_slice_header_partial(nal_data, nal_type)
        elif nal_type == 6:
            print("  (SEI - skipping)")


if __name__ == '__main__':
    files = [
        'd:/Craig/GitHub/Sub0h264/tests/fixtures/bouncing_ball.h264',
        'd:/Craig/GitHub/Sub0h264/tests/fixtures/scrolling_texture.h264',
        'd:/Craig/GitHub/Sub0h264/tests/fixtures/bouncing_ball_ionly.h264',
        'd:/Craig/GitHub/Sub0h264/tests/fixtures/scrolling_texture_ionly.h264',
    ]
    for f in files:
        try:
            analyze_file(f)
        except Exception as e:
            print(f"  ERROR: {e}")
