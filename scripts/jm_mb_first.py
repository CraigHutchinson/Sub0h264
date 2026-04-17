#!/usr/bin/env python3
"""Print the first N tokens for one or more MBs in JM trace_dec.txt POC 0.

Usage: python scripts/jm_mb_first.py <trace_dec.txt> <mb_addr1,mb_addr2,...> [tokens_per_mb=8]
"""
import re
import sys

path = sys.argv[1]
mbs = [int(x) for x in sys.argv[2].split(',')]
n_tok = int(sys.argv[3]) if len(sys.argv) > 3 else 8

text = open(path, encoding='utf-8', errors='replace').read()
poc0 = text[text.find('POC: 0'):]
nxt = poc0.find('POC: 1', 100)
if nxt > 0:
    poc0 = poc0[:nxt]

for mb in mbs:
    needle = f"POC: 0 (I/P) MB: {mb} Slice: 0"
    idx = poc0.find(needle)
    if idx < 0:
        print(f"MB {mb}: NOT FOUND")
        continue
    nxt = poc0.find("POC: 0 (I/P) MB:", idx + 10)
    chunk = poc0[idx : nxt if nxt > 0 else idx + 4000]
    lines = chunk.strip().split('\n')[:n_tok+1]
    print(f"--- MB {mb} ---")
    for ln in lines:
        print(f"  {ln}")
