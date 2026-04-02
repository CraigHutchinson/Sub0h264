"""Check RBSP bits at a specific offset and try VLC table matching."""
import sys
import os

def load_rbsp(fixture_path):
    data = open(fixture_path, 'rb').read()
    # Find IDR NAL (3-byte start code)
    for sc in range(len(data) - 3):
        if data[sc] == 0 and data[sc+1] == 0 and data[sc+2] == 1:
            if (data[sc+3] & 0x1F) == 5:
                raw = bytearray(data[sc+4:])  # skip SC + nal header
                end = len(raw)
                for e in range(2, min(len(raw)-2, 10000)):
                    if raw[e] == 0 and raw[e+1] == 0 and raw[e+2] in (0, 1):
                        end = e
                        break
                raw = raw[:end]
                rbsp = bytearray()
                k = 0
                while k < len(raw):
                    if k+2 < len(raw) and raw[k] == 0 and raw[k+1] == 0 and raw[k+2] == 3:
                        rbsp.append(0); rbsp.append(0); k += 3
                    else:
                        rbsp.append(raw[k]); k += 1
                return rbsp
    return None

def gb(rbsp, p):
    return (rbsp[p // 8] >> (7 - (p % 8))) & 1

def read_bits(rbsp, start, n):
    v = 0
    for i in range(n):
        v = (v << 1) | gb(rbsp, start + i)
    return v

def main():
    project = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    fixture = os.path.join(project, "tests", "fixtures", "baseline_640x480_short.h264")

    rbsp = load_rbsp(fixture)
    if rbsp is None:
        print("ERROR: could not find IDR NAL")
        sys.exit(1)

    print(f"RBSP: {len(rbsp)} bytes = {len(rbsp)*8} bits")

    offset = int(sys.argv[1]) if len(sys.argv) > 1 else 624
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 16

    bits_str = ''.join(str(gb(rbsp, offset + i)) for i in range(count))
    print(f"\nBits {offset}-{offset+count-1}: {bits_str}")
    print(f"  As hex: 0x{read_bits(rbsp, offset, min(count, 16)):04x}")

    # Try coeff_token table 9-5(c) matching (nC 4-7) — from libavc
    sizes_c = [
        [4, 6, 6, 6, 7, 7, 7, 7, 8, 8, 9, 9, 9, 10, 10, 10, 10],  # t1=0
        [0, 4, 5, 5, 5, 5, 6, 6, 7, 8, 8, 9, 9, 9, 10, 10, 10],   # t1=1
        [0, 0, 4, 5, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 10],   # t1=2
        [0, 0, 0, 4, 4, 4, 4, 4, 5, 6, 7, 8, 8, 9, 10, 10, 10],   # t1=3
    ]
    codes_c = [
        [15, 15, 11, 8, 15, 11, 9, 8, 15, 11, 15, 11, 8, 13, 9, 5, 1],   # t1=0
        [0, 14, 15, 12, 10, 8, 14, 10, 14, 14, 10, 14, 10, 7, 12, 8, 4], # t1=1
        [0, 0, 13, 14, 11, 9, 13, 9, 13, 10, 13, 9, 13, 9, 11, 7, 3],   # t1=2
        [0, 0, 0, 12, 11, 10, 9, 8, 13, 12, 12, 12, 8, 12, 10, 6, 2],   # t1=3
    ]

    peek16 = read_bits(rbsp, offset, 16)
    print(f"\nTable 9-5(c) (nC 4-7) matches at bit {offset}:")
    print(f"  peek16 = {peek16:016b}")

    best_tc, best_t1, best_sz = -1, -1, 255
    for t1 in range(4):
        for tc in range(17):
            if t1 > tc:
                continue
            sz = sizes_c[t1][tc]
            if sz == 0 or sz > 16:
                continue
            code = codes_c[t1][tc]
            mask = (1 << sz) - 1
            peeked = (peek16 >> (16 - sz)) & mask
            if peeked == code:
                marker = ""
                if sz < best_sz:
                    best_tc, best_t1, best_sz = tc, t1, sz
                    marker = " <-- BEST"
                print(f"  tc={tc:2d} t1={t1} size={sz:2d} code={code:>{sz}b}={code:4d} {marker}")

    if best_sz < 255:
        print(f"\n  Best match: tc={best_tc}, t1={best_t1}, size={best_sz}")
    else:
        print(f"\n  NO MATCH FOUND! Bitstream may be corrupt at this position.")

if __name__ == "__main__":
    main()
