#!/usr/bin/env python3
"""Compare our cabac_init_mn.hpp (m,n) values against libavc pre-computed table.

Reads libavc's gau1_ih264d_cabac_ctxt_init_table for init_idc=3 (I-slice), QP=17
and compares against our computed values from cCabacInitMN.

SPDX-License-Identifier: MIT
"""
import re
import sys


def compute_init_state(m, n, qp):
    """Compute mpsState from (m, n) and QP per spec §9.3.1.1."""
    pre = ((m * qp) >> 4) + n
    pre = max(1, min(126, pre))
    if pre <= 63:
        p_state = 63 - pre
        val_mps = 0
    else:
        p_state = pre - 64
        val_mps = 1
    return (p_state & 0x3F) | (val_mps << 6)


def load_our_mn(path="components/sub0h264/src/cabac_init_mn.hpp", idc=3):
    """Load (m,n) values for given init_idc from our C++ header."""
    with open(path) as f:
        content = f.read()
    # Split by init_idc markers
    sections = content.split("// init_idc ")
    for s in sections[1:]:
        if s.startswith(str(idc)):
            pairs = re.findall(r'\{\s*(-?\d+),\s*(-?\d+)\}', s)
            return [(int(m), int(n)) for m, n in pairs[:460]]
    return []


def load_libavc_table(path="docs/reference/libavc/decoder/ih264d_cabac_init_tables.c"):
    """Load libavc's pre-computed init table.
    Returns dict of (init_idc, qp, ctxIdx) -> mpsState."""
    with open(path) as f:
        content = f.read()

    # Find the table definition
    start = content.find("gau1_ih264d_cabac_ctxt_init_table")
    if start < 0:
        print("Table not found in libavc source")
        return {}

    # Extract all numeric values from the table
    # The table format is [4][52][460] = init_idc × QP × ctxIdx
    table_content = content[start:]
    # Find the opening brace
    brace_start = table_content.find('{')
    if brace_start < 0:
        return {}
    table_content = table_content[brace_start:]

    # Extract all numeric values
    nums = re.findall(r'\b(\d+)\b', table_content)
    nums = [int(n) for n in nums]

    # Parse as [4][52][460]
    result = {}
    idx = 0
    for idc in range(4):
        for qp in range(52):
            for ctx in range(460):
                if idx < len(nums):
                    result[(idc, qp, ctx)] = nums[idx]
                    idx += 1
    return result


def main():
    qp = 17
    idc = 3  # I-slice

    our_mn = load_our_mn()
    libavc = load_libavc_table()

    if not our_mn:
        print("Failed to load our (m,n) table")
        return
    if not libavc:
        print("Failed to load libavc table")
        return

    print(f"Comparing init_idc={idc}, QP={qp}: our (m,n) vs libavc pre-computed")
    print(f"{'ctxIdx':>6} {'our_mn':>10} {'our_state':>10} {'libavc':>10} {'match':>6}")

    mismatches = 0
    # Check specific key contexts
    key_contexts = {
        5: "mb_type_I",
        68: "prev_intra4x4",
        64: "intra_chroma_0",
        73: "cbp_luma_0",
        77: "cbp_chroma_0",
        60: "qp_delta",
        93: "cbf_cat2",
        94: "cbf_cat2+1",
        134: "sig_cat2[0]",
        135: "sig_cat2[1]",
        140: "sig_cat2[6]",
        195: "last_cat2[0]",
        200: "last_cat2[5]",
        247: "level_cat2[0]",
        248: "level_cat2[1]",
        252: "level_cat2[5]",
    }

    for ctx_idx in sorted(key_contexts.keys()):
        m, n = our_mn[ctx_idx]
        our_state = compute_init_state(m, n, qp)
        libavc_state = libavc.get((idc, qp, ctx_idx), -1)
        match = "OK" if our_state == libavc_state else "DIFF"
        if our_state != libavc_state:
            mismatches += 1
        desc = key_contexts.get(ctx_idx, "")
        print(f"{ctx_idx:>6} ({m:>3},{n:>4}) state={our_state:>3}  libavc={libavc_state:>3}  {match}  {desc}")

    # Full comparison
    print(f"\nFull comparison (all 460 contexts):")
    full_mismatches = 0
    for ctx_idx in range(460):
        m, n = our_mn[ctx_idx]
        our_state = compute_init_state(m, n, qp)
        libavc_state = libavc.get((idc, qp, ctx_idx), -1)
        if our_state != libavc_state:
            full_mismatches += 1
            if full_mismatches <= 20:
                print(f"  MISMATCH ctxIdx={ctx_idx}: ({m},{n}) -> our={our_state}, libavc={libavc_state}")

    if full_mismatches > 20:
        print(f"  ... and {full_mismatches - 20} more mismatches")

    print(f"\nTotal mismatches: {full_mismatches} / 460")
    if full_mismatches == 0:
        print("ALL MATCH — our (m,n) table produces identical init states to libavc")
    else:
        print(f"WARNING: {full_mismatches} context init values differ from libavc!")


if __name__ == "__main__":
    main()
