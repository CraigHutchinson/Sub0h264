#!/usr/bin/env python3
"""Measure PSNR between ffmpeg-decoded output and raw source YUV.
This tells us the encoding quality ceiling (the maximum PSNR a correct decoder can achieve).
"""
import sys
import struct
import math
import os

def compute_psnr(raw_path, decoded_path, width, height, nframes):
    """Compute per-frame Y-PSNR between raw source and decoded YUV."""
    frame_size = width * height + 2 * (width // 2) * (height // 2)
    y_size = width * height

    with open(raw_path, 'rb') as f_raw, open(decoded_path, 'rb') as f_dec:
        for i in range(nframes):
            raw_frame = f_raw.read(frame_size)
            dec_frame = f_dec.read(frame_size)
            if len(raw_frame) < frame_size or len(dec_frame) < frame_size:
                break

            # Y-plane only PSNR
            raw_y = raw_frame[:y_size]
            dec_y = dec_frame[:y_size]

            mse = 0.0
            for r, d in zip(raw_y, dec_y):
                diff = r - d
                mse += diff * diff
            mse /= y_size

            if mse == 0:
                psnr = float('inf')
            else:
                psnr = 10.0 * math.log10(255.0 * 255.0 / mse)

            print(f"  Frame {i}: PSNR={psnr:.2f} dB")


if __name__ == '__main__':
    W, H, N = 320, 240, 30

    print("=== Bouncing ball I-only: ffmpeg decode vs raw source ===")
    compute_psnr(
        'd:/Craig/GitHub/Sub0h264/tests/fixtures/bouncing_ball_ionly_raw.yuv',
        'd:/Craig/GitHub/Sub0h264/build/ffmpeg_bb_ionly.yuv',
        W, H, N)

    print("\n=== Scrolling texture I-only: ffmpeg decode vs raw source ===")
    # Decode scroll too
    os.system('ffmpeg -y -i d:/Craig/GitHub/Sub0h264/tests/fixtures/scrolling_texture_ionly.h264 '
              '-pix_fmt yuv420p d:/Craig/GitHub/Sub0h264/build/ffmpeg_scroll_ionly.yuv 2>/dev/null')
    compute_psnr(
        'd:/Craig/GitHub/Sub0h264/tests/fixtures/scrolling_texture_ionly_raw.yuv',
        'd:/Craig/GitHub/Sub0h264/build/ffmpeg_scroll_ionly.yuv',
        W, H, N)
