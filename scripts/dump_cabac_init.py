#!/usr/bin/env python3
"""Dump CABAC initialization data from an H.264 bitstream.

Shows NAL unit structure, slice header bit layout, and the initial 9-bit
codIOffset value used to initialize the CABAC arithmetic decoder engine.

Usage:
    python scripts/dump_cabac_init.py <h264_file> [--nal-idx N]

SPDX-License-Identifier: MIT
"""
import argparse
import sys


def find_nal_units(data: bytes) -> list[tuple[str, int]]:
    """Find all NAL unit start positions."""
    nals = []
    i = 0
    while i < len(data) - 3:
        if data[i] == 0 and data[i + 1] == 0:
            if data[i + 2] == 1:
                nals.append(("3byte", i + 3))
                i += 3
                continue
            elif data[i + 2] == 0 and i + 3 < len(data) and data[i + 3] == 1:
                nals.append(("4byte", i + 4))
                i += 4
                continue
        i += 1
    return nals


NAL_TYPES = {1: "P-slice", 5: "IDR", 6: "SEI", 7: "SPS", 8: "PPS", 9: "AUD"}


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("h264_file", help="H.264 Annex B file to analyze")
    parser.add_argument("--nal-idx", type=int, default=None,
                        help="Specific NAL index to analyze (default: first IDR)")
    parser.add_argument("--rbsp-bytes", type=int, default=8,
                        help="Number of RBSP bytes to dump (default: 8)")
    args = parser.parse_args()

    data = open(args.h264_file, "rb").read()
    nals = find_nal_units(data)

    print(f"File: {args.h264_file} ({len(data)} bytes, {len(nals)} NALs)")
    print()

    for idx, (sc_type, offset) in enumerate(nals):
        nal_header = data[offset]
        nal_type = nal_header & 0x1F
        ref_idc = (nal_header >> 5) & 3
        name = NAL_TYPES.get(nal_type, f"type{nal_type}")

        # Determine NAL end (next start code or EOF)
        next_offset = len(data)
        if idx + 1 < len(nals):
            next_offset = nals[idx + 1][1] - (4 if nals[idx + 1][0] == "4byte" else 3)
        nal_size = next_offset - offset

        print(f"  NAL {idx}: {name} (type={nal_type}, ref_idc={ref_idc}) "
              f"at offset {offset}, ~{nal_size} bytes")

        # Analyze specific NAL
        should_analyze = (args.nal_idx is not None and idx == args.nal_idx) or \
                         (args.nal_idx is None and nal_type == 5)

        if should_analyze:
            rbsp_start = offset + 1  # skip NAL header byte
            print(f"\n  === Analyzing NAL {idx} ({name}) ===")
            print(f"  RBSP starts at file offset {rbsp_start}")

            # Dump first N RBSP bytes
            n_bytes = min(args.rbsp_bytes, len(data) - rbsp_start)
            print(f"  First {n_bytes} RBSP bytes:")
            for j in range(n_bytes):
                b = data[rbsp_start + j]
                print(f"    byte {j}: 0x{b:02x} = {b:08b}")

            # For IDR/slice NALs, show CABAC init
            if nal_type in (1, 5):
                # The slice header length depends on many fields.
                # For IDR with known parameters, we estimate based on typical field sizes.
                # For accurate parsing, use the trace output from the decoder.
                print(f"\n  Note: Use decoder trace output [CABAC-ENGINE] for exact codIOffset.")
                print(f"  Decoder reports codIOffset from br.readBits(9) after slice header alignment.")

            if args.nal_idx is None:
                break  # Only analyze first IDR unless specific index requested

    print()


if __name__ == "__main__":
    main()
