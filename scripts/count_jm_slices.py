#!/usr/bin/env python3
"""Count JM_BIN entries per JM_SLICE_START in a JM bin trace.

Usage: python scripts/count_jm_slices.py <jm_bin.txt> [max_slices]

Helps map JM's per-slice bin counts back to actual H.264 slices/frames so
the lockstep_compare slice index is set correctly.
"""
import re
import sys

path = sys.argv[1]
limit = int(sys.argv[2]) if len(sys.argv) > 2 else 30

slice_pat = re.compile(r"JM_SLICE_START slice=(\d+)")
current = None
counts = []
with open(path, encoding="utf-8", errors="replace") as fp:
    for line in fp:
        m = slice_pat.search(line)
        if m:
            counts.append([int(m.group(1)), 0])
            current = counts[-1]
            continue
        if line.startswith("JM_BIN") and current is not None:
            current[1] += 1

for i, (s, n) in enumerate(counts[:limit]):
    print(f"slice={s:4d} bins={n}")
print(f"... total {len(counts)} JM_SLICE_START events")
