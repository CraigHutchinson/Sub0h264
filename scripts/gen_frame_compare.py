#!/usr/bin/env python3
"""Generate frame comparison images: decoded vs ffmpeg reference.

Usage:
    python scripts/gen_frame_compare.py [--fixture NAME] [--frame N]

Generates:
    build/frame_compare/ours.png          Our decoded frame (RGB)
    build/frame_compare/ref.png           ffmpeg reference (RGB)
    build/frame_compare/diff_4x.png       |ours - ref| × 4 (RGB)
    build/frame_compare/ours_Y.png        Y-plane grayscale
    build/frame_compare/ref_Y.png         Y-plane grayscale
    build/frame_compare/diff_Y_4x.png     Y-plane diff × 4
    build/frame_compare/compare.png       Side-by-side (ours | ref | diff)
    build/frame_compare/compare_Y.png     Side-by-side Y-plane
    build/frame_compare/top_zoom.png      Top 128px zoomed
    build/frame_compare/stats.txt         Diff statistics

Requires: numpy, Pillow, ffmpeg in PATH.
"""
import argparse
import os
import subprocess
import sys

import numpy as np
from PIL import Image

W, H = 640, 480


def load_yuv420(path):
    raw = np.fromfile(path, dtype=np.uint8)
    y_size = W * H
    uv_size = (W // 2) * (H // 2)
    y = raw[:y_size].reshape(H, W)
    u = raw[y_size:y_size + uv_size].reshape(H // 2, W // 2)
    v = raw[y_size + uv_size:y_size + 2 * uv_size].reshape(H // 2, W // 2)
    return y, u, v


def yuv_to_rgb(y, u, v):
    u_full = np.repeat(np.repeat(u, 2, axis=0), 2, axis=1).astype(np.float32)
    v_full = np.repeat(np.repeat(v, 2, axis=0), 2, axis=1).astype(np.float32)
    yf = y.astype(np.float32)
    r = yf + 1.402 * (v_full - 128.0)
    g = yf - 0.344136 * (u_full - 128.0) - 0.714136 * (v_full - 128.0)
    b = yf + 1.772 * (u_full - 128.0)
    return np.clip(np.stack([r, g, b], axis=-1), 0, 255).astype(np.uint8)


def abs_diff(a, b, scale=4):
    return np.clip(np.abs(a.astype(np.int16) - b.astype(np.int16)) * scale,
                   0, 255).astype(np.uint8)


def side_by_side(imgs, margin=4, bg=40):
    """Concatenate images horizontally with margin."""
    h = imgs[0].size[1]
    w_total = sum(im.size[0] for im in imgs) + margin * (len(imgs) - 1)
    mode = imgs[0].mode
    canvas = Image.new(mode, (w_total, h), bg if mode == "L" else (bg, bg, bg))
    x = 0
    for im in imgs:
        canvas.paste(im, (x, 0))
        x += im.size[0] + margin
    return canvas


def write_stats(path, y_ours, y_ref):
    diff = np.abs(y_ours.astype(np.int16) - y_ref.astype(np.int16))
    nonzero = np.count_nonzero(diff)
    total = W * H
    lines = [
        f"Y-plane diff statistics",
        f"=======================",
        f"Pixels differ:  {nonzero}/{total} ({100*nonzero/total:.1f}%)",
        f"Max diff:       {diff.max()}",
        f"Mean diff:      {diff.mean():.2f}",
    ]
    if nonzero > 0:
        lines.append(f"Mean (nonzero): {diff[diff>0].mean():.2f}")
        for row in range(H):
            if diff[row].max() > 0:
                lines.append(f"First diff row: y={row}")
                break
        worst = np.unravel_index(diff.argmax(), diff.shape)
        lines.append(f"Worst pixel:    ({worst[1]}, {worst[0]}) diff={diff[worst]}")

        # Last row with content in our frame
        for row in range(H - 1, -1, -1):
            if y_ours[row].max() > 0:
                lines.append(f"Last non-zero Y row: {row} (MB row {row//16})")
                break

        # Per-MB-row summary
        lines.append(f"\nPer-MB-row diff summary:")
        for mby in range(H // 16):
            rs, re = mby * 16, (mby + 1) * 16
            mb_diff = diff[rs:re, :]
            n = np.count_nonzero(mb_diff)
            if n > 0:
                lines.append(f"  MB row {mby:2d} (y={rs:3d}-{re-1:3d}): "
                             f"{n:5d} diffs, max={mb_diff.max():3d}, "
                             f"mean={mb_diff[mb_diff>0].mean():.1f}")
            else:
                lines.append(f"  MB row {mby:2d}: all zero (no decode)")
                break

    text = "\n".join(lines)
    with open(path, "w") as f:
        f.write(text + "\n")
    print(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fixture", default="baseline_640x480_short",
                        help="Fixture name without .h264 extension")
    parser.add_argument("--frame", type=int, default=0,
                        help="Frame index to compare")
    args = parser.parse_args()

    project = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    outdir = os.path.join(project, "build", "frame_compare")
    os.makedirs(outdir, exist_ok=True)

    fixture = os.path.join(project, "tests", "fixtures",
                           args.fixture + ".h264")
    if not os.path.isfile(fixture):
        print(f"ERROR: fixture not found: {fixture}", file=sys.stderr)
        sys.exit(1)

    # --- Generate ffmpeg reference ---
    ref_yuv = os.path.join(outdir, "ref.yuv")
    cmd = ["ffmpeg", "-y", "-i", fixture, "-vframes", "1",
           "-pix_fmt", "yuv420p", ref_yuv]
    print(f"Generating reference: {' '.join(cmd)}")
    subprocess.run(cmd, capture_output=True, check=True)

    # --- Get our decoded frame ---
    ours_yuv = os.path.join(project, "build", "tests", "Release",
                            "debug_baseline_frame0.yuv")
    if not os.path.isfile(ours_yuv):
        print(f"ERROR: decoded frame not found: {ours_yuv}")
        print("Run: build/tests/Release/Sub0h264_Tests.exe "
              '-tc="Debug: dump baseline frame 0*"')
        sys.exit(1)

    # --- Load frames ---
    print(f"Loading ours:  {ours_yuv}")
    ours_y, ours_u, ours_v = load_yuv420(ours_yuv)
    print(f"Loading ref:   {ref_yuv}")
    ref_y, ref_u, ref_v = load_yuv420(ref_yuv)

    # --- Convert to RGB ---
    ours_rgb = yuv_to_rgb(ours_y, ours_u, ours_v)
    ref_rgb = yuv_to_rgb(ref_y, ref_u, ref_v)

    # --- Save individual images ---
    Image.fromarray(ours_rgb).save(os.path.join(outdir, "ours.png"))
    Image.fromarray(ref_rgb).save(os.path.join(outdir, "ref.png"))
    Image.fromarray(ours_y).save(os.path.join(outdir, "ours_Y.png"))
    Image.fromarray(ref_y).save(os.path.join(outdir, "ref_Y.png"))

    # --- Diff images ---
    diff_rgb = abs_diff(ours_rgb, ref_rgb, scale=4)
    diff_y = abs_diff(ours_y, ref_y, scale=4)
    Image.fromarray(diff_rgb).save(os.path.join(outdir, "diff_4x.png"))
    Image.fromarray(diff_y).save(os.path.join(outdir, "diff_Y_4x.png"))

    # --- Side-by-side composites ---
    ours_im = Image.fromarray(ours_rgb)
    ref_im = Image.fromarray(ref_rgb)
    diff_im = Image.fromarray(diff_rgb)
    side_by_side([ours_im, ref_im, diff_im]).save(
        os.path.join(outdir, "compare.png"))

    ours_y_im = Image.fromarray(ours_y)
    ref_y_im = Image.fromarray(ref_y)
    diff_y_im = Image.fromarray(diff_y)
    side_by_side([ours_y_im, ref_y_im, diff_y_im]).save(
        os.path.join(outdir, "compare_Y.png"))

    # --- Zoomed top region ---
    top = 128
    side_by_side([
        ours_im.crop((0, 0, W, top)),
        ref_im.crop((0, 0, W, top)),
        diff_im.crop((0, 0, W, top)),
    ]).save(os.path.join(outdir, "top_zoom.png"))

    print(f"\nImages saved to {outdir}/")

    # --- Stats ---
    write_stats(os.path.join(outdir, "stats.txt"), ours_y, ref_y)

    print(f"\nFiles:")
    for f in sorted(os.listdir(outdir)):
        sz = os.path.getsize(os.path.join(outdir, f))
        print(f"  {f:20s} {sz:>8,d} bytes")


if __name__ == "__main__":
    main()
