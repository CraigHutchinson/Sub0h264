"""Lock-step CABAC comparison: our decoder vs JM reference decoder.

Usage:
    python scripts/lockstep_compare.py <our_trace.txt> <jm_trace.txt> [max_bins=N] [jm_slice=N]

Reads both traces, aligns at slice start, and reports the first divergence point.

Our trace format (from sub0h264_trace --level entropy):
    OUR_SLICE_START slice=N R=510 O=...
    <binIdx> <pre_state_raw> <post_mpsState_raw> <decoded_bit> <post_range> <post_offset> <ctxIdx>
    <binIdx> BP <decoded_bit> <post_range> <post_offset>

JM trace format (from modified JM ldecod — see tools/jm_trace/README.md):
    JM_SLICE_START slice=N R=510 O=...
    JM_BIN N: s=state mps=mps R=pre_range V=value bl=bits_left rLPS=lps -> bit=B R2=R2 V2=V2 bl2=bl2
    (bypass bins: s=-1 mps=-1)

Key relationship:
    JM O_pre = V >> bl
    JM O_post (O2) = V2 >> bl2
    Our post_offset = JM O2 (both are the effective arithmetic offset after renorm)
    Our post_range = JM R2 (both are the arithmetic range after renorm)
"""

import sys
import re

def parse_our_trace(path):
    """Parse our decoder's bin trace into a list of dicts."""
    bins = []
    found_start = False
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.strip()
            if line.startswith('OUR_SLICE_START'):
                m = re.search(r'R=(\d+)\s+O=(\d+)', line)
                if m:
                    print(f"[OUR]  Slice start: R={m.group(1)} O={m.group(2)}")
                found_start = True
                continue
            if not found_start:
                continue
            if line.startswith('#') or line.startswith('['):
                continue
            # Normal bin: "binIdx pre_state post_mpsState bit post_range post_offset ctxIdx"
            parts = line.split()
            if len(parts) >= 7 and parts[1] != 'BP':
                try:
                    b = {
                        'idx': int(parts[0]),
                        'pre_state_raw': int(parts[1]),
                        'pre_state': int(parts[1]) & 63,
                        'pre_mps': (int(parts[1]) >> 6) & 1,
                        'bit': int(parts[3]),
                        'R2': int(parts[4]),
                        'O2': int(parts[5]),
                        'ctx': int(parts[6]),
                        'bypass': False,
                    }
                    bins.append(b)
                except ValueError:
                    pass
            # Bypass bin: "binIdx BP bit post_range post_offset"
            elif len(parts) >= 5 and parts[1] == 'BP':
                try:
                    b = {
                        'idx': int(parts[0]),
                        'pre_state': -1,
                        'pre_mps': -1,
                        'bit': int(parts[2]),
                        'R2': int(parts[3]),
                        'O2': int(parts[4]),
                        'ctx': -1,
                        'bypass': True,
                    }
                    bins.append(b)
                except ValueError:
                    pass
    return bins


def parse_jm_trace(path, target_slice=1):
    """Parse JM's bin trace into a list of dicts.

    With the jm_trace.patch applied (tools/jm_trace/), both the fast MPS path and
    the renorm path have the jm_slice_count guard, so JM_SLICE_START slice=1
    correctly introduces the first P-frame's bins. Set target_slice=1 (default)
    to compare against the first P-frame.
    """
    bins = []
    collecting = False
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.strip()
            if line.startswith('JM_SLICE_START'):
                m_slice = re.search(r'slice=(\d+)', line)
                slice_num = int(m_slice.group(1)) if m_slice else -1
                m = re.search(r'R=(\d+)\s+O=(\d+)', line)
                if slice_num == target_slice:
                    if m:
                        print(f"[JM]   Slice start (target slice={target_slice}): R={m.group(1)} O={m.group(2)}")
                    collecting = True
                elif collecting:
                    # Hit the next slice boundary — stop
                    print(f"[JM]   Stopping at JM_SLICE_START slice={slice_num} (collected {len(bins)} bins)")
                    break
                else:
                    if m:
                        print(f"[JM]   Skipping JM_SLICE_START slice={slice_num}: R={m.group(1)} O={m.group(2)}")
                continue
            if not collecting:
                continue
            if not line.startswith('JM_BIN'):
                continue
            # JM_BIN N: s=... mps=... R=... V=... bl=... rLPS=... -> bit=... R2=... V2=... bl2=...
            m = re.match(
                r'JM_BIN (\d+): s=(-?\d+) mps=(-?\d+) R=(\d+) V=(\d+) bl=(\d+) rLPS=(\d+) -> bit=(\d+) R2=(\d+) V2=(\d+) bl2=(\d+)',
                line
            )
            if m:
                V2 = int(m.group(10))
                bl2 = int(m.group(11))
                b = {
                    'idx': int(m.group(1)),
                    'pre_state': int(m.group(2)),
                    'pre_mps': int(m.group(3)),
                    'pre_R': int(m.group(4)),
                    'pre_O': int(m.group(5)) >> int(m.group(6)),
                    'rLPS': int(m.group(7)),
                    'bit': int(m.group(8)),
                    'R2': int(m.group(9)),
                    'O2': V2 >> bl2,
                    'bypass': (int(m.group(2)) == -1),
                }
                bins.append(b)
    return bins


def compare(our_bins, jm_bins, max_show=30):
    """Compare bins and find first divergence."""
    n = min(len(our_bins), len(jm_bins))
    print(f"\nComparing {n} bins (our={len(our_bins)}, jm={len(jm_bins)})")
    print(f"{'Idx':>6}  {'JM s':>5} {'JM mps':>6} {'JM R2':>6} {'JM O2':>6} {'JM bit':>6} | "
          f"{'Our s':>5} {'Our mps':>7} {'Our R2':>6} {'Our O2':>6} {'Our bit':>7} | {'Match':>6}")
    print('-' * 95)

    first_divergence = None
    shown = 0
    matches = 0

    for i in range(n):
        j = jm_bins[i]
        o = our_bins[i]

        # Compare decoded bit, post-range, post-offset, pre-state, pre-mps
        bit_ok  = (j['bit'] == o['bit'])
        R2_ok   = (j['R2'] == o['R2'])
        O2_ok   = (j['O2'] == o['O2'])
        # State comparison (bypass bins have s=-1 in both)
        if j['bypass'] or o['bypass']:
            state_ok = (j['bypass'] == o['bypass'])
        else:
            state_ok = (j['pre_state'] == o['pre_state']) and (j['pre_mps'] == o['pre_mps'])

        all_ok = bit_ok and R2_ok and O2_ok and state_ok

        if all_ok:
            matches += 1
        else:
            if first_divergence is None:
                first_divergence = i

        # Always print the first 5 bins, then only divergent bins (up to max_show)
        if i < 5 or not all_ok:
            if shown < max_show:
                marker = '  OK' if all_ok else ' <<< DIVERGE'
                print(f"{j['idx']:>6}  {j['pre_state']:>5} {j['pre_mps']:>6} {j['R2']:>6} {j['O2']:>6} {j['bit']:>6} | "
                      f"{o['pre_state']:>5} {o['pre_mps']:>7} {o['R2']:>6} {o['O2']:>6} {o['bit']:>7} | {marker}")
                shown += 1
            if not all_ok and shown >= max_show:
                print(f"  ... (more divergences, use larger max_bins)")
                break

    print(f"\nResult: {matches}/{n} bins match")
    if first_divergence is not None:
        print(f"FIRST DIVERGENCE at bin {first_divergence}:")
        j = jm_bins[first_divergence]
        o = our_bins[first_divergence]
        print(f"  JM:  s={j['pre_state']} mps={j['pre_mps']} R_pre={j.get('pre_R','?')} O_pre={j.get('pre_O','?')} rLPS={j.get('rLPS','?')} -> bit={j['bit']} R2={j['R2']} O2={j['O2']}")
        print(f"  Our: s={o['pre_state']} mps={o['pre_mps']} -> bit={o['bit']} R2={o['R2']} O2={o['O2']} ctx={o.get('ctx','?')}")
        if o['pre_state'] != j['pre_state']:
            print(f"  -> Context (state) mismatch: JM s={j['pre_state']} vs Our s={o['pre_state']}")
        if o['pre_mps'] != j['pre_mps']:
            print(f"  -> MPS mismatch: JM mps={j['pre_mps']} vs Our mps={o['pre_mps']}")
        if not (o['R2'] == j['R2'] and o['O2'] == j['O2']):
            print(f"  -> Arithmetic mismatch: JM R2={j['R2']} O2={j['O2']} vs Our R2={o['R2']} O2={o['O2']}")
        if o['bit'] != j['bit']:
            print(f"  -> Decoded bit mismatch: JM={j['bit']} Our={o['bit']}")
    else:
        print("All bins matched!")
    return first_divergence


def main():
    if len(sys.argv) < 3:
        print("Usage: python scripts/lockstep_compare.py our_trace.txt jm_trace.txt [max_bins=N] [jm_slice=N]")
        sys.exit(1)

    our_path = sys.argv[1]
    jm_path = sys.argv[2]
    max_bins = 200000
    jm_slice = 1  # With jm_trace.patch applied, P-frame 1 bins appear after JM_SLICE_START slice=1
    for arg in sys.argv[3:]:
        if arg.startswith('jm_slice='):
            jm_slice = int(arg.split('=', 1)[1])
        else:
            max_bins = int(arg)

    print(f"Loading our trace: {our_path}")
    our_bins = parse_our_trace(our_path)
    print(f"  Parsed {len(our_bins)} bins")

    print(f"Loading JM trace:  {jm_path}  (target JM_SLICE_START slice={jm_slice})")
    jm_bins = parse_jm_trace(jm_path, target_slice=jm_slice)
    print(f"  Parsed {len(jm_bins)} bins")

    if max_bins < len(our_bins):
        our_bins = our_bins[:max_bins]
    if max_bins < len(jm_bins):
        jm_bins = jm_bins[:max_bins]

    compare(our_bins, jm_bins)


if __name__ == '__main__':
    main()
