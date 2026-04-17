#!/usr/bin/env python3
"""Map bin index → MB number using mb_type bin0 markers (ctx 3, 4, or 5 in I-slice).

Usage: python scripts/map_bin_to_mb.py <our_bin.txt> <bin_idx>

Reads sub0h264_trace --level entropy output and counts mb_type-bin0 events
(ctx ∈ {3,4,5} in I-slice CABAC) to estimate which MB the given bin index
falls in. Useful for cross-referencing lockstep_compare divergences with the
JM `*** MB: N ***` markers in trace_dec.txt.
"""
import sys

path = sys.argv[1]
target = int(sys.argv[2])

with open(path) as fp:
    mb_count = 0
    last_mb_bin = -1
    for ln in fp:
        parts = ln.split()
        if len(parts) < 7 or parts[1] == 'BP':
            continue
        try:
            idx = int(parts[0])
            ctx = int(parts[6])
        except ValueError:
            continue
        if ctx in (3, 4, 5):
            mb_count += 1
            if last_mb_bin >= 0 and target <= idx:
                print(f"target bin {target} is in MB {mb_count - 2} (mb_type bin0 at our_idx {last_mb_bin}-{idx-1})")
                break
            last_mb_bin = idx
        if idx >= target and mb_count > 0:
            print(f"target bin {target} reached, currently in MB {mb_count - 1} starting at our_idx {last_mb_bin}")
            print(f"bin {target} offset within MB: {target - last_mb_bin}")
            break
