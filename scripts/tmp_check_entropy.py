#!/usr/bin/env python3
"""Check entropy coding mode for all H.264 fixtures."""
import glob
import os

def read_ue(bits, pos):
    zeros = 0
    while pos < len(bits) and bits[pos] == 0:
        zeros += 1
        pos += 1
    pos += 1  # skip 1 bit
    val = 0
    for _ in range(zeros):
        val = (val << 1) | bits[pos]
        pos += 1
    return val, pos

def check_file(path):
    data = open(path, 'rb').read()
    # Find NALs
    i = 0
    while i < len(data) - 4:
        if data[i] == 0 and data[i+1] == 0:
            if data[i+2] == 1:
                nal_off = i + 3
            elif data[i+2] == 0 and data[i+3] == 1:
                nal_off = i + 4
            else:
                i += 1
                continue
            nal_type = data[nal_off] & 0x1F
            if nal_type == 8:  # PPS
                rbsp = data[nal_off+1:nal_off+20]
                bits = []
                for b in rbsp:
                    for bi in range(7, -1, -1):
                        bits.append((b >> bi) & 1)
                pos = 0
                pps_id, pos = read_ue(bits, pos)
                sps_id, pos = read_ue(bits, pos)
                entropy = bits[pos]
                return 'CABAC' if entropy else 'CAVLC'
            i = nal_off + 1
        else:
            i += 1
    return 'unknown'

fixtures = sorted(glob.glob('tests/fixtures/*.h264'))
for f in fixtures:
    name = os.path.basename(f)
    mode = check_file(f)
    raw = f.replace('.h264', '_raw.yuv')
    has_raw = 'Y' if os.path.exists(raw) else 'N'
    size = os.path.getsize(f)
    print(f"{name:45s} {mode:6s}  raw={has_raw}  {size:>10,} bytes")
