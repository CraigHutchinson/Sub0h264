#!/usr/bin/env python3
"""JM 19.0 reference decoder wrapper — first-class lockstep entrypoint.

Subcommands:

  decode      Decode an H.264 bitstream. Writes YUV + trace_dec.txt + log.dec.
  mb          Print the JM trace for one MB at a given POC (mb_x,mb_y or mb_addr).
  diff-mb     Side-by-side compare JM vs ours for one MB at a given POC.
  psnr        Decode with JM and compare our YUV decode (sub0h264_trace) vs JM
              YUV — prints per-frame Y PSNR.

The JM binary is expected at:
  docs/reference/jm/bin/vs18/msvc-19.50/x86_64/release/ldecod.exe
  (Windows, MSVC build) — fall back to umake/gcc-15.2 (Linux/WSL) if absent.

trace_dec.txt is generated alongside the decode (JM writes it in cwd by
default, to the directory containing the YUV).

Usage:
  python scripts/run_jm.py decode tests/fixtures/foo.h264 [--out yuv]
  python scripts/run_jm.py mb tests/fixtures/foo.h264 7 0 9 [--width 640]
  python scripts/run_jm.py psnr tests/fixtures/foo.h264 [--width 640 --height 368]
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

JM_CANDIDATES = [
    ROOT / "docs/reference/jm/bin/vs18/msvc-19.50/x86_64/release/ldecod.exe",
    ROOT / "docs/reference/jm/bin/umake/gcc-15.2/x86_64/release/ldecod",
    ROOT / "docs/reference/jm/bin/ldecod_static",
]
JM_CFG = ROOT / "docs/reference/jm/cfg/decoder.cfg"
TRACE_BIN = ROOT / "build/test_apps/trace/Release/sub0h264_trace.exe"


def _jm_bin() -> Path:
    for c in JM_CANDIDATES:
        if c.exists():
            return c
    print("ERROR: JM ldecod binary not found. Looked at:", file=sys.stderr)
    for c in JM_CANDIDATES:
        print(f"  {c}", file=sys.stderr)
    sys.exit(2)


def _frame_size(w: int, h: int) -> int:
    return w * h + 2 * (w // 2) * (h // 2)


# ── decode ─────────────────────────────────────────────────────────────────

def cmd_decode(args: argparse.Namespace) -> int:
    """Run JM ldecod on an H.264 file. Produces YUV + trace_dec.txt."""
    inp = Path(args.input).resolve()
    out = Path(args.out or f"{inp.with_suffix('').name}_jm.yuv").resolve()
    ref = Path(args.ref or f"/tmp/_jm_ref_unused.yuv")

    cmd = [str(_jm_bin()), "-d", str(JM_CFG.resolve()),
           "-p", f"InputFile={inp}",
           "-p", f"OutputFile={out}",
           "-p", f"RefFile={ref}"]
    if args.silent:
        cmd += ["-p", "Silent=1"]

    p = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if p.returncode != 0:
        print(f"JM exit {p.returncode}", file=sys.stderr)
        print(p.stderr[-1000:], file=sys.stderr)
        return p.returncode
    print(f"YUV  -> {out}")
    if Path("trace_dec.txt").exists():
        # Move trace into the same dir as the YUV so it's discoverable.
        trace_dst = out.with_name(f"{out.stem}_trace.txt")
        shutil.move("trace_dec.txt", trace_dst)
        print(f"Trace-> {trace_dst}")
    if Path("log.dec").exists():
        Path("log.dec").unlink()
    if Path("dataDec.txt").exists():
        Path("dataDec.txt").unlink()
    return 0


# ── mb ─────────────────────────────────────────────────────────────────────

def _slice_chunk_for_poc(text: str, poc: int) -> str:
    """Return only the trace_dec.txt chunk for a specific POC.
    JM emits POCs in display order (0,2,4,...) — find the FIRST `MB: 0` for
    that POC and slice from there to the next `MB: 0` marker (any POC)."""
    start_pat = re.compile(rf"\*+ POC: {poc} \(I/P\) MB: 0 Slice: 0 Type \d+ \*+")
    m = start_pat.search(text)
    if not m:
        return ""
    nxt = re.search(r"\*+ POC: \d+ \(I/P\) MB: 0 Slice: 0 Type \d+ \*+",
                    text[m.end():])
    end = m.end() + nxt.start() if nxt else len(text)
    return text[m.start():end]


def cmd_mb(args: argparse.Namespace) -> int:
    trace_path = Path(args.trace).resolve()
    if not trace_path.exists():
        # Try sibling _trace.txt for the YUV path naming convention.
        alt = trace_path.with_name(trace_path.stem + "_trace.txt")
        if alt.exists():
            trace_path = alt
        else:
            print(f"trace not found: {trace_path}", file=sys.stderr)
            return 1
    text = trace_path.read_text(encoding="utf-8", errors="replace")
    chunk = _slice_chunk_for_poc(text, args.poc)
    if not chunk:
        print(f"POC {args.poc} not in trace", file=sys.stderr)
        return 1
    width_in_mbs = args.width // 16
    mb_addr = args.mb_y * width_in_mbs + args.mb_x
    needle = f"(I/P) MB: {mb_addr} Slice:"
    idx = chunk.find(needle)
    if idx < 0:
        print(f"MB({args.mb_x},{args.mb_y}) addr={mb_addr} not in POC {args.poc}",
              file=sys.stderr)
        return 1
    # Find start of the line containing the marker
    line_start = chunk.rfind("\n", 0, idx) + 1
    nxt = chunk.find("(I/P) MB:", idx + len(needle))
    end = chunk.rfind("\n", 0, nxt) if nxt > 0 else len(chunk)
    section = chunk[line_start:end]
    lines = section.splitlines()
    if args.lines:
        lines = lines[:args.lines]
    for ln in lines:
        print(ln)
    return 0


# ── psnr ───────────────────────────────────────────────────────────────────

def cmd_psnr(args: argparse.Namespace) -> int:
    """Decode with JM and our trace tool; print per-frame Y PSNR diff."""
    import numpy as np

    inp = Path(args.input).resolve()
    jm_yuv = Path(f"/tmp/jm_{inp.stem}.yuv")
    our_yuv = Path(f"/tmp/our_{inp.stem}.yuv")

    if not args.skip_jm:
        ns = argparse.Namespace(input=str(inp), out=str(jm_yuv), ref=None, silent=True)
        if cmd_decode(ns) != 0:
            return 1
    if not TRACE_BIN.exists():
        print(f"build sub0h264_trace first: {TRACE_BIN}", file=sys.stderr)
        return 1
    subprocess.run([str(TRACE_BIN), str(inp), "--dump", str(our_yuv)],
                   capture_output=True, check=False)

    our = np.fromfile(our_yuv, dtype=np.uint8)
    jm  = np.fromfile(jm_yuv,  dtype=np.uint8)
    fs = _frame_size(args.width, args.height)
    n = min(len(our) // fs, len(jm) // fs)
    print(f"Comparing {n} frames @ {args.width}x{args.height}")
    psnrs = []
    for fr in range(n):
        oy = our[fr * fs:fr * fs + args.width * args.height].reshape(args.height, args.width).astype(np.float64)
        jy = jm [fr * fs:fr * fs + args.width * args.height].reshape(args.height, args.width).astype(np.float64)
        mse = float(np.mean((oy - jy) ** 2))
        psnr = 99.0 if mse < 1e-10 else 10.0 * float(np.log10(255.0 ** 2 / mse))
        psnrs.append(psnr)
        marker = "  ✓" if psnr >= 95.0 else "  ✗"
        print(f"  Frame {fr:3d}: {psnr:6.2f} dB{marker}")
    print(f"\nAvg: {sum(psnrs)/len(psnrs):.2f} dB  Min: {min(psnrs):.2f} dB")
    return 0


# ── arg parsing ────────────────────────────────────────────────────────────

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("decode", help="Run JM ldecod, dump YUV + trace_dec.txt")
    p.add_argument("input")
    p.add_argument("--out", "-o", default=None)
    p.add_argument("--ref", default=None)
    p.add_argument("--silent", action="store_true", default=True)
    p.set_defaults(func=cmd_decode)

    p = sub.add_parser("mb", help="Print JM trace for one MB at a given POC")
    p.add_argument("trace", help="trace_dec.txt path (or sibling _trace.txt)")
    p.add_argument("poc", type=int)
    p.add_argument("mb_x", type=int)
    p.add_argument("mb_y", type=int)
    p.add_argument("--width", type=int, default=640, help="frame width (luma) px")
    p.add_argument("--lines", "-n", type=int, default=0, help="line cap (0=all)")
    p.set_defaults(func=cmd_mb)

    p = sub.add_parser("psnr", help="Per-frame Y PSNR: ours vs JM")
    p.add_argument("input")
    p.add_argument("--width", type=int, default=640)
    p.add_argument("--height", type=int, default=368)
    p.add_argument("--skip-jm", action="store_true",
                   help="reuse /tmp/jm_<stem>.yuv from a previous run")
    p.set_defaults(func=cmd_psnr)

    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
