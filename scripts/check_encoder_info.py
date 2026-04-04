#!/usr/bin/env python3
"""Check encoder identity in an H.264 bitstream by searching for known strings.

Looks for x264, JM, ffmpeg, openh264 and other encoder signatures in
SEI user_data_unregistered and raw byte patterns.

Usage:
    python scripts/check_encoder_info.py <h264_file>

SPDX-License-Identifier: MIT
"""
import argparse
import sys


KNOWN_ENCODERS = [b"x264", b"JM ", b"ffmpeg", b"libx264", b"openh264",
                  b"MainConcept", b"Intel", b"NVENC", b"h264_qsv"]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("h264_file", help="H.264 Annex B file to check")
    args = parser.parse_args()

    data = open(args.h264_file, "rb").read()
    print(f"File: {args.h264_file} ({len(data)} bytes)")

    found = False
    for enc in KNOWN_ENCODERS:
        idx = data.find(enc)
        if idx >= 0:
            # Show surrounding context
            start = max(0, idx - 4)
            end = min(len(data), idx + len(enc) + 60)
            context = data[start:end]
            # Filter to printable ASCII
            printable = "".join(chr(b) if 32 <= b < 127 else "." for b in context)
            print(f"  Found '{enc.decode()}' at offset {idx}: ...{printable}...")
            found = True

    if not found:
        print("  No known encoder signature found in bitstream")


if __name__ == "__main__":
    main()
