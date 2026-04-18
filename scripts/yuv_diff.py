#!/usr/bin/env python3
"""YUV-vs-YUV diagnostics: PSNR, first-divergence, top-MB-diffs, MB pixel dump.

Subcommands:

  psnr   Y-plane PSNR per frame (handles ours-vs-cropped-ref height mismatch).
  first  List the first MBs whose Y-plane diff exceeds a threshold.
  top    Top-N MBs of a frame ranked by mean abs-diff (catches outliers).
  dump   Side-by-side pixel dump of one MB across two YUVs.

This consolidates the most-used tmp_*.py YUV diagnostics into one place.
Kept as a single file because every subcommand reuses the YUV layout helpers.

Usage examples:
  python scripts/yuv_diff.py psnr ours.yuv ref.yuv 640 368 360
  python scripts/yuv_diff.py first ours.yuv ref.yuv 640 368 360 --frame 0 --threshold 1
  python scripts/yuv_diff.py top   ours.yuv ref.yuv 640 368 360 --frame 9 --top 10
  python scripts/yuv_diff.py dump  ours.yuv ref.yuv 640 368 360 13 0 --frame 0
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Iterator

import numpy as np


# ── YUV layout helpers ──────────────────────────────────────────────────────

def _frame_size(w: int, h: int) -> int:
    return w * h + 2 * (w // 2) * (h // 2)


def _y_plane(buf: np.ndarray, frame: int, w: int, h: int) -> np.ndarray:
    fs = _frame_size(w, h)
    return buf[frame * fs:frame * fs + w * h].reshape(h, w).astype(np.int32)


def _load(path: str | Path) -> np.ndarray:
    return np.fromfile(path, dtype=np.uint8)


# ── psnr ────────────────────────────────────────────────────────────────────

def cmd_psnr(args: argparse.Namespace) -> int:
    our = _load(args.ours)
    ref = _load(args.ref)
    w, h, h_ref = args.width, args.height, args.ref_height
    fs_our = _frame_size(w, h)
    fs_ref = _frame_size(w, h_ref)
    n = min(len(our) // fs_our, len(ref) // fs_ref)
    h_compare = min(h, h_ref)

    psnrs = []
    for fr in range(n):
        oy = our[fr * fs_our:fr * fs_our + w * h_compare].reshape(h_compare, w).astype(np.float64)
        ry = ref[fr * fs_ref:fr * fs_ref + w * h_compare].reshape(h_compare, w).astype(np.float64)
        mse = float(np.mean((oy - ry) ** 2))
        psnr = 99.0 if mse < 1e-10 else 10.0 * float(np.log10(255.0 ** 2 / mse))
        psnrs.append(psnr)
        if args.verbose or fr < 5 or fr == n - 1:
            print(f"  Frame {fr:3d}: Y PSNR = {psnr:.2f} dB")
    if not psnrs:
        print("no frames decoded", file=sys.stderr)
        return 1
    print(f"\nAvg: {sum(psnrs) / len(psnrs):.2f} dB  Min: {min(psnrs):.2f} dB  ({n} frames)")
    return 0


# ── first / top ─────────────────────────────────────────────────────────────

def _diff_iter(args: argparse.Namespace) -> Iterator[tuple[int, int, int, float, np.ndarray]]:
    """Yield (mb_x, mb_y, max, mean, block) for every MB with diff > 0."""
    our = _load(args.ours)
    ref = _load(args.ref)
    w, h, h_ref = args.width, args.height, args.ref_height
    fs_our = _frame_size(w, h)
    fs_ref = _frame_size(w, h_ref)
    h_compare = min(h, h_ref)
    fr = args.frame
    oy = our[fr * fs_our:fr * fs_our + w * h_compare].reshape(h_compare, w).astype(np.int32)
    ry = ref[fr * fs_ref:fr * fs_ref + w * h_compare].reshape(h_compare, w).astype(np.int32)
    diff = np.abs(oy - ry)
    for my in range(h_compare // 16):
        for mx in range(w // 16):
            block = diff[my * 16:(my + 1) * 16, mx * 16:(mx + 1) * 16]
            if int(block.max()) > 0:
                yield mx, my, int(block.max()), float(block.mean()), block


def cmd_first(args: argparse.Namespace) -> int:
    n_shown = 0
    total = 0
    for mx, my, mxd, mnd, _ in _diff_iter(args):
        total += 1
        if mxd >= args.threshold and n_shown < args.limit:
            print(f"  MB({mx:2d},{my:2d}): max={mxd:3d} mean={mnd:.2f}")
            n_shown += 1
    print(f"\n{total} MBs with any diff in frame {args.frame}")
    return 0


def cmd_top(args: argparse.Namespace) -> int:
    items = list(_diff_iter(args))
    items.sort(key=lambda x: -x[3])
    print(f"Top {min(len(items), args.top)} MBs by mean diff (frame {args.frame}):")
    for mx, my, mxd, mnd, _ in items[:args.top]:
        print(f"  MB({mx:2d},{my:2d}): max={mxd:3d} mean={mnd:6.2f}")
    if not items:
        print("  (no diffs)")
    return 0


# ── dump ────────────────────────────────────────────────────────────────────

def cmd_dump(args: argparse.Namespace) -> int:
    our = _load(args.ours)
    ref = _load(args.ref)
    w, h, h_ref = args.width, args.height, args.ref_height
    fr = args.frame
    fs_our = _frame_size(w, h)
    fs_ref = _frame_size(w, h_ref)
    oy = our[fr * fs_our:fr * fs_our + w * h].reshape(h, w)
    ry = ref[fr * fs_ref:fr * fs_ref + w * h_ref].reshape(h_ref, w)
    px, py = args.mb_x * 16, args.mb_y * 16
    print(f"MB({args.mb_x},{args.mb_y}) frame {fr} @ pixel ({px},{py})")
    print("ours:")
    for r in range(16):
        print(f"  r{r:2d}: " + " ".join(f"{int(oy[py + r, c]):3d}" for c in range(px, px + 16)))
    print("ref:")
    for r in range(16):
        print(f"  r{r:2d}: " + " ".join(f"{int(ry[py + r, c]):3d}" for c in range(px, px + 16)))
    print("diff (ours - ref):")
    for r in range(16):
        print(f"  r{r:2d}: "
              + " ".join(f"{int(oy[py + r, c]) - int(ry[py + r, c]):+4d}" for c in range(px, px + 16)))
    return 0


# ── arg parsing ─────────────────────────────────────────────────────────────

def _add_common(p: argparse.ArgumentParser) -> None:
    p.add_argument("ours", help="Our decoded YUV file")
    p.add_argument("ref", help="Reference YUV file")
    p.add_argument("width", type=int, help="Frame width (luma) in pixels")
    p.add_argument("height", type=int, help="Our frame height in pixels")
    p.add_argument("ref_height", type=int, nargs="?", default=None,
                   help="Reference frame height (defaults to height; specify when "
                        "ffmpeg honours frame_cropping, e.g. ours=368, ref=360)")


def _post_parse(args: argparse.Namespace) -> None:
    if args.ref_height is None:
        args.ref_height = args.height


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("psnr", help="Per-frame Y PSNR")
    _add_common(p)
    p.add_argument("--verbose", "-v", action="store_true", help="Print every frame")
    p.set_defaults(func=cmd_psnr)

    p = sub.add_parser("first", help="First N MBs over a diff threshold (raster order)")
    _add_common(p)
    p.add_argument("--frame", "-f", type=int, default=0)
    p.add_argument("--threshold", "-t", type=int, default=1, help="Min max-diff to print")
    p.add_argument("--limit", "-n", type=int, default=20)
    p.set_defaults(func=cmd_first)

    p = sub.add_parser("top", help="Top-N MBs by mean diff for one frame")
    _add_common(p)
    p.add_argument("--frame", "-f", type=int, default=0)
    p.add_argument("--top", "-n", type=int, default=10)
    p.set_defaults(func=cmd_top)

    p = sub.add_parser("dump", help="Pixel dump of one 16x16 MB from both YUVs")
    _add_common(p)
    p.add_argument("mb_x", type=int)
    p.add_argument("mb_y", type=int)
    p.add_argument("--frame", "-f", type=int, default=0)
    p.set_defaults(func=cmd_dump)

    args = ap.parse_args()
    _post_parse(args)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
