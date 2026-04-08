#!/usr/bin/env python3
"""Generate a visual diff image comparing our decode vs ffmpeg.

Creates a side-by-side PNG: [ours | ffmpeg | diff (amplified)]
with per-MB grid overlay and PSNR annotation.

Usage:
    python scripts/gen_diff_image.py <our.yuv> <ref.yuv> <width> <height> <output.png> [--raw <raw.yuv>]

SPDX-License-Identifier: MIT
"""
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont


def load_y_plane(path, w, h, frame=0):
    y_size = w * h
    frame_size = y_size + 2 * (w // 2) * (h // 2)
    with open(path, "rb") as f:
        f.seek(frame * frame_size)
        return np.frombuffer(f.read(y_size), dtype=np.uint8).reshape(h, w)


def psnr(a, b):
    mse = np.mean((a.astype(float) - b.astype(float)) ** 2)
    return 10 * np.log10(255 ** 2 / mse) if mse > 0 else 999.0


def main():
    if len(sys.argv) < 6:
        print(f"Usage: {sys.argv[0]} our.yuv ref.yuv width height output.png [--raw raw.yuv]")
        sys.exit(1)

    our_path = sys.argv[1]
    ref_path = sys.argv[2]
    w = int(sys.argv[3])
    h = int(sys.argv[4])
    out_path = sys.argv[5]
    raw_path = None
    if "--raw" in sys.argv:
        raw_path = sys.argv[sys.argv.index("--raw") + 1]

    our = load_y_plane(our_path, w, h)
    ref = load_y_plane(ref_path, w, h)

    # Compute diff (amplified for visibility)
    diff = our.astype(np.int16) - ref.astype(np.int16)
    diff_vis = np.clip(diff * 2 + 128, 0, 255).astype(np.uint8)  # gray=128 = no diff

    # Absolute diff (bright = large error)
    abs_diff = np.clip(np.abs(diff) * 4, 0, 255).astype(np.uint8)

    # Create side-by-side image: [ours | ffmpeg | diff | abs_diff]
    panel_w = w
    total_w = panel_w * 4 + 6  # 3 pixel gaps
    img = Image.new("L", (total_w, h + 20), 0)

    # Paste panels
    img.paste(Image.fromarray(our), (0, 0))
    img.paste(Image.fromarray(ref), (panel_w + 2, 0))
    img.paste(Image.fromarray(diff_vis), (panel_w * 2 + 4, 0))
    img.paste(Image.fromarray(abs_diff), (panel_w * 3 + 6, 0))

    # Draw MB grid on diff panel
    draw = ImageDraw.Draw(img)
    for mbx in range(w // 16 + 1):
        x = panel_w * 2 + 4 + mbx * 16
        draw.line([(x, 0), (x, h)], fill=64)
        x2 = panel_w * 3 + 6 + mbx * 16
        draw.line([(x2, 0), (x2, h)], fill=64)
    for mby in range(h // 16 + 1):
        y = mby * 16
        draw.line([(panel_w * 2 + 4, y), (panel_w * 3 + 3, y)], fill=64)
        draw.line([(panel_w * 3 + 6, y), (panel_w * 4 + 5, y)], fill=64)

    # Annotate
    p = psnr(our, ref)
    draw.text((2, h + 2), f"Ours", fill=200)
    draw.text((panel_w + 4, h + 2), f"ffmpeg", fill=200)
    draw.text((panel_w * 2 + 6, h + 2), f"Diff (2x)", fill=200)
    draw.text((panel_w * 3 + 8, h + 2), f"|Diff| 4x  PSNR={p:.1f}dB", fill=200)

    if raw_path:
        raw = load_y_plane(raw_path, w, h)
        p_raw = psnr(our, raw)
        p_ref_raw = psnr(ref, raw)
        draw.text((2, h + 10), f"vs raw: ours={p_raw:.1f}dB ff={p_ref_raw:.1f}dB", fill=180)

    img.save(out_path)
    print(f"Saved diff image to {out_path}")
    print(f"PSNR (ours vs ref): {p:.1f} dB")


if __name__ == "__main__":
    main()
