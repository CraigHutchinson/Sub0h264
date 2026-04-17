#!/usr/bin/env python3
"""Summarise mb_type and intra4x4_pred_mode for each MB in JM trace POC 0.

Usage: python scripts/jm_mb_summary.py <trace_dec.txt> <start_mb> <end_mb>

Prints one line per MB:
  MB N: type=X t8x8=Y modes=[...] cbp=Z qp_delta=Q
"""
import re
import sys

path, start, end = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
text = open(path, encoding='utf-8', errors='replace').read()
poc0 = text[text.find('POC: 0'):]

for mb in range(start, end + 1):
    needle = f"POC: 0 (I/P) MB: {mb} Slice: 0"
    idx = poc0.find(needle)
    if idx < 0:
        continue
    nxt = poc0.find("POC: 0 (I/P) MB:", idx + 10)
    chunk = poc0[idx : nxt if nxt > 0 else idx + 4000]

    fields = {}
    modes = []
    for ln in chunk.split('\n'):
        m = re.search(r'@\d+\s+(\S+)\s+\(\s*(-?\d+)\)', ln)
        if not m:
            continue
        name, val = m.group(1), int(m.group(2))
        if name == 'intra4x4_pred_mode':
            modes.append(val)
        elif name in ('mb_type', 'transform_size_8x8_flag', 'coded_block_pattern',
                      'mb_qp_delta', 'intra_chroma_pred_mode'):
            fields[name] = val

    print(f"MB {mb:4d}: type={fields.get('mb_type','?'):2} t8x8={fields.get('transform_size_8x8_flag',0)} "
          f"chroma={fields.get('intra_chroma_pred_mode','?')} cbp={fields.get('coded_block_pattern',0):2} "
          f"qpD={fields.get('mb_qp_delta',0):3} modes={modes}")
