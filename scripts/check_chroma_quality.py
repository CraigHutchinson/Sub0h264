#!/usr/bin/env python3
"""Check per-plane PSNR for scrolling texture to verify chroma bug.

Usage:
    python scripts/check_chroma_quality.py

SPDX-License-Identifier: MIT
"""
import subprocess
import sys
import numpy as np


def psnr(a, b):
    mse = np.mean((a.astype(np.float64) - b.astype(np.float64)) ** 2)
    if mse == 0:
        return 999.0
    return 10 * np.log10(255**2 / mse)


def load_yuv420(path, width, height, frame=0):
    y_size = width * height
    uv_size = (width // 2) * (height // 2)
    frame_size = y_size + 2 * uv_size
    with open(path, "rb") as f:
        f.seek(frame * frame_size)
        data = f.read(frame_size)
    if len(data) < frame_size:
        return None, None, None
    y = np.frombuffer(data[:y_size], dtype=np.uint8).reshape(height, width)
    u = np.frombuffer(data[y_size:y_size+uv_size], dtype=np.uint8).reshape(height//2, width//2)
    v = np.frombuffer(data[y_size+uv_size:], dtype=np.uint8).reshape(height//2, width//2)
    return y, u, v


def decode_first_frame(h264_path, width, height):
    cmd = [
        "ffmpeg", "-y", "-i", h264_path,
        "-vframes", "1", "-pix_fmt", "yuv420p",
        "-f", "rawvideo", "pipe:1"
    ]
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    y_size = width * height
    uv_size = (width // 2) * (height // 2)
    frame_size = y_size + 2 * uv_size
    data = result.stdout
    if len(data) < frame_size:
        return None, None, None
    y = np.frombuffer(data[:y_size], dtype=np.uint8).reshape(height, width)
    u = np.frombuffer(data[y_size:y_size+uv_size], dtype=np.uint8).reshape(height//2, width//2)
    v = np.frombuffer(data[y_size+uv_size:], dtype=np.uint8).reshape(height//2, width//2)
    return y, u, v


def main():
    W, H = 320, 240

    fixtures = [
        ("scrolling_texture_ionly", "tests/fixtures/scrolling_texture_ionly.h264",
         "tests/fixtures/scrolling_texture_ionly_raw.yuv"),
        ("bouncing_ball_ionly", "tests/fixtures/bouncing_ball_ionly.h264",
         "tests/fixtures/bouncing_ball_ionly_raw.yuv"),
    ]

    for name, h264, raw_path in fixtures:
        print(f"\n=== {name} ===")

        # Load raw source
        raw_y, raw_u, raw_v = load_yuv420(raw_path, W, H, 0)
        if raw_y is None:
            print(f"  Cannot read raw source {raw_path}")
            continue

        # Load our decode (from trace tool dump)
        dump_path = f"build/debug_{name}.yuv"
        import os
        trace_exe = os.path.join("build", "test_apps", "trace", "Release", "sub0h264_trace.exe")
        subprocess.run([trace_exe, h264, "--dump", dump_path], capture_output=True)
        ours_y, ours_u, ours_v = load_yuv420(dump_path, W, H, 0)

        # Load ffmpeg decode
        ff_y, ff_u, ff_v = decode_first_frame(h264, W, H)

        if ours_y is not None:
            print(f"  Our decoder vs raw source:")
            print(f"    Y PSNR: {psnr(ours_y, raw_y):.1f} dB")
            print(f"    U PSNR: {psnr(ours_u, raw_u):.1f} dB")
            print(f"    V PSNR: {psnr(ours_v, raw_v):.1f} dB")

        if ff_y is not None:
            print(f"  ffmpeg vs raw source:")
            print(f"    Y PSNR: {psnr(ff_y, raw_y):.1f} dB")
            print(f"    U PSNR: {psnr(ff_u, raw_u):.1f} dB")
            print(f"    V PSNR: {psnr(ff_v, raw_v):.1f} dB")

        if ours_y is not None and ff_y is not None:
            print(f"  Our decoder vs ffmpeg:")
            print(f"    Y PSNR: {psnr(ours_y, ff_y):.1f} dB")
            print(f"    U PSNR: {psnr(ours_u, ff_u):.1f} dB")
            print(f"    V PSNR: {psnr(ours_v, ff_v):.1f} dB")

            # Check chroma diff pattern
            u_diff = ours_u.astype(np.int16) - ff_u.astype(np.int16)
            v_diff = ours_v.astype(np.int16) - ff_v.astype(np.int16)
            print(f"  Chroma U diff: min={u_diff.min()} max={u_diff.max()} mean={u_diff.mean():.1f}")
            print(f"  Chroma V diff: min={v_diff.min()} max={v_diff.max()} mean={v_diff.mean():.1f}")

            # Per-MB chroma check (8x8 MB blocks for chroma)
            print(f"  Per-MB chroma U diff (first 2 rows):")
            for mby in range(2):
                for mbx in range(W // 16):
                    blk = u_diff[mby*8:(mby+1)*8, mbx*8:(mbx+1)*8]
                    mx = np.max(np.abs(blk))
                    if mx > 0:
                        print(f"    MB({mbx},{mby}): max_abs={mx} mean={blk.mean():.1f}")
                    else:
                        print(f"    MB({mbx},{mby}): PERFECT")


if __name__ == "__main__":
    main()
