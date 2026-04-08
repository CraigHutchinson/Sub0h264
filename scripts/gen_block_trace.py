#!/usr/bin/env python3
"""Generate a diffable per-block trace from our decoder's callback output.

Parses the test output and produces a compact one-line-per-block format:
  MB(x,y) scan=N raster=R nC=NC tc=TC bits=B pred_crc=XXXX out_crc=XXXX dq_crc=XXXX

This can be diffed against the same format from libavc.

Usage:
    python scripts/gen_block_trace.py < test_output.txt

SPDX-License-Identifier: MIT
"""
import sys
import re
import zlib


def crc16(data_bytes):
    """CRC-16 of a byte sequence."""
    return zlib.crc32(data_bytes) & 0xFFFF


def main():
    for line in sys.stdin:
        line = line.strip()
        # Parse MESSAGE lines
        m = re.search(r'MB\((\d+)\) scan(\d+): nC=(\d+) tc=(\d+) bits=(\d+)', line)
        if m:
            mbx, scan, nc, tc, bits = m.groups()
            print(f"MB({mbx},0) scan={scan} nC={nc} tc={tc} bits={bits}")


if __name__ == "__main__":
    main()
