#!/usr/bin/env python3
"""Generate synthetic H.264 test fixtures with known raw YUV source.

Creates test sequences, encodes to H.264 baseline profile, and stores
both the raw source YUV and the encoded H.264 for ground-truth comparison.

The decoder's output should match the raw source to within the encoding
loss (quantization). PSNR vs raw source is the true quality metric.

Usage:
    python scripts/gen_synthetic_fixtures.py [--outdir tests/fixtures]

Requires: ffmpeg with libx264, numpy, Pillow.

SPDX-License-Identifier: MIT
"""
import argparse
import os
import subprocess
import sys
import struct

import numpy as np

W, H = 320, 240  # Small size for fast test encode/decode
FPS = 25
NUM_FRAMES = 30
QP = 20  # Low QP for high quality (less quantization loss)


def make_checkerboard_texture(w, h, block_size=16):
    """Generate a checkerboard texture pattern."""
    img = np.zeros((h, w, 3), dtype=np.uint8)
    for y in range(h):
        for x in range(w):
            if ((x // block_size) + (y // block_size)) % 2 == 0:
                img[y, x] = [200, 100, 50]
            else:
                img[y, x] = [50, 150, 200]
    return img


def make_noise_background(w, h, seed=42):
    """Generate static noise background."""
    rng = np.random.RandomState(seed)
    return rng.randint(30, 226, (h, w, 3), dtype=np.uint8)


def rgb_to_yuv420(rgb):
    """Convert RGB frame to I420 YUV bytes."""
    h, w = rgb.shape[:2]
    r, g, b = rgb[:,:,0].astype(np.float32), rgb[:,:,1].astype(np.float32), rgb[:,:,2].astype(np.float32)

    y = np.clip(0.257 * r + 0.504 * g + 0.098 * b + 16, 0, 255).astype(np.uint8)
    u = np.clip(-0.148 * r - 0.291 * g + 0.439 * b + 128, 0, 255).astype(np.uint8)
    v = np.clip(0.439 * r - 0.368 * g - 0.071 * b + 128, 0, 255).astype(np.uint8)

    # Subsample chroma 4:2:0
    u_sub = u[0::2, 0::2]
    v_sub = v[0::2, 0::2]

    return y.tobytes() + u_sub.tobytes() + v_sub.tobytes()


def gen_scrolling_texture(outdir, name="scrolling_texture", intra_only=False):
    """Scrolling checkerboard: exercises I and P frame prediction with movement."""
    print(f"Generating {name}...")
    texture = make_checkerboard_texture(W * 2, H * 2, block_size=12)

    raw_path = os.path.join(outdir, f"{name}_raw.yuv")
    h264_path = os.path.join(outdir, f"{name}.h264")

    with open(raw_path, "wb") as f:
        for frame_idx in range(NUM_FRAMES):
            ox = (frame_idx * 4) % W
            oy = (frame_idx * 2) % H
            crop = texture[oy:oy+H, ox:ox+W]
            f.write(rgb_to_yuv420(crop))

    encode_yuv_to_h264(raw_path, h264_path, W, H, NUM_FRAMES, intra_only=intra_only)
    print(f"  Raw: {raw_path} ({os.path.getsize(raw_path)} bytes)")
    print(f"  H264: {h264_path} ({os.path.getsize(h264_path)} bytes)")
    return name


def gen_bouncing_ball(outdir, name="bouncing_ball", intra_only=False):
    """Bouncing ball over noisy background: exercises motion estimation."""
    print(f"Generating {name}...")
    bg = make_noise_background(W, H, seed=123)

    raw_path = os.path.join(outdir, f"{name}_raw.yuv")
    h264_path = os.path.join(outdir, f"{name}.h264")

    ball_r = 20
    bx, by = 50.0, 50.0
    vx, vy = 5.0, 3.5

    with open(raw_path, "wb") as f:
        for frame_idx in range(NUM_FRAMES):
            frame = bg.copy()

            # Draw ball (filled circle)
            cx, cy = int(bx), int(by)
            for y in range(max(0, cy - ball_r), min(H, cy + ball_r)):
                for x in range(max(0, cx - ball_r), min(W, cx + ball_r)):
                    if (x - cx)**2 + (y - cy)**2 <= ball_r**2:
                        frame[y, x] = [255, 80, 80]  # Red ball

            f.write(rgb_to_yuv420(frame))

            # Bounce
            bx += vx
            by += vy
            if bx - ball_r < 0 or bx + ball_r >= W:
                vx = -vx
                bx += vx * 2
            if by - ball_r < 0 or by + ball_r >= H:
                vy = -vy
                by += vy * 2

    encode_yuv_to_h264(raw_path, h264_path, W, H, NUM_FRAMES, intra_only=intra_only)
    print(f"  Raw: {raw_path} ({os.path.getsize(raw_path)} bytes)")
    print(f"  H264: {h264_path} ({os.path.getsize(h264_path)} bytes)")
    return name


def gen_gradient_pan(outdir, name="gradient_pan", intra_only=False):
    """Panning gradient: smooth tones exercise prediction accuracy."""
    print(f"Generating {name}...")
    # Create a wide gradient image
    gradient = np.zeros((H, W * 3, 3), dtype=np.uint8)
    for x in range(W * 3):
        t = x / (W * 3 - 1)
        gradient[:, x, 0] = int(255 * t)        # R ramp
        gradient[:, x, 1] = int(128 * (1 - t))  # G inverse
        gradient[:, x, 2] = int(200 * abs(0.5 - t) * 2)  # B peak at edges

    raw_path = os.path.join(outdir, f"{name}_raw.yuv")
    h264_path = os.path.join(outdir, f"{name}.h264")

    with open(raw_path, "wb") as f:
        for frame_idx in range(NUM_FRAMES):
            ox = (frame_idx * 6) % (W * 2)
            crop = gradient[:, ox:ox+W]
            f.write(rgb_to_yuv420(crop))

    encode_yuv_to_h264(raw_path, h264_path, W, H, NUM_FRAMES, intra_only=intra_only)
    print(f"  Raw: {raw_path} ({os.path.getsize(raw_path)} bytes)")
    print(f"  H264: {h264_path} ({os.path.getsize(h264_path)} bytes)")
    return name


def encode_yuv_to_h264(yuv_path, h264_path, w, h, nframes, intra_only=False):
    """Encode raw YUV to H.264 baseline profile using ffmpeg/libx264."""
    gop = "1" if intra_only else "10"
    cmd = [
        "ffmpeg", "-y",
        "-f", "rawvideo",
        "-pix_fmt", "yuv420p",
        "-s", f"{w}x{h}",
        "-r", str(FPS),
        "-i", yuv_path,
        "-frames:v", str(nframes),
        "-c:v", "libx264",
        "-profile:v", "baseline",
        "-level", "3.0",
        "-preset", "medium",
        "-qp", str(QP),
        "-g", gop,
        "-bf", "0",  # No B-frames (baseline)
        "-an",
        h264_path,
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"ffmpeg encode failed:\n{result.stderr}", file=sys.stderr)
        sys.exit(1)


def compute_raw_crcs(raw_path, w, h, nframes):
    """Compute per-frame CRC32 of raw YUV source for ground-truth comparison."""
    import zlib
    frame_size = w * h + 2 * (w // 2) * (h // 2)
    crcs = []
    with open(raw_path, "rb") as f:
        for i in range(nframes):
            data = f.read(frame_size)
            if len(data) < frame_size:
                break
            crcs.append(zlib.crc32(data) & 0xFFFFFFFF)
    return crcs


def gen_pan_direction(outdir, name, dx, dy, intra_only=False):
    """Pan in any direction at specified speed. Exercises negative MVs,
    vertical-only motion, fast diagonal, etc.
    dx, dy: pixels per frame of camera pan (positive = content moves left/up).
    Reference: tests MV prediction §8.4.1.3, MC §8.4.2, skip MV §8.4.1.1."""
    print(f"Generating {name} (dx={dx}, dy={dy})...")
    # Create oversized texture that wraps
    tex_w = W + abs(dx) * NUM_FRAMES + 32
    tex_h = H + abs(dy) * NUM_FRAMES + 32
    texture = make_checkerboard_texture(tex_w, tex_h, block_size=16)

    raw_path = os.path.join(outdir, f"{name}_raw.yuv")
    h264_path = os.path.join(outdir, f"{name}.h264")

    with open(raw_path, "wb") as f:
        for frame_idx in range(NUM_FRAMES):
            ox = max(0, frame_idx * dx) if dx >= 0 else max(0, -frame_idx * dx)
            oy = max(0, frame_idx * dy) if dy >= 0 else max(0, -frame_idx * dy)
            # Clamp to valid region
            ox = min(ox, tex_w - W)
            oy = min(oy, tex_h - H)
            crop = texture[oy:oy+H, ox:ox+W]
            f.write(rgb_to_yuv420(crop))

    encode_yuv_to_h264(raw_path, h264_path, W, H, NUM_FRAMES, intra_only=intra_only)
    print(f"  Raw: {raw_path} ({os.path.getsize(raw_path)} bytes)")
    print(f"  H264: {h264_path} ({os.path.getsize(h264_path)} bytes)")
    return name


def gen_static_scene(outdir, name="static_scene", intra_only=False):
    """Static scene: no motion. Tests skip MV=(0,0) derivation §8.4.1.1,
    zero-MV MC, and residual-only coding with no motion compensation."""
    print(f"Generating {name}...")
    texture = make_checkerboard_texture(W, H, block_size=24)

    raw_path = os.path.join(outdir, f"{name}_raw.yuv")
    h264_path = os.path.join(outdir, f"{name}.h264")

    with open(raw_path, "wb") as f:
        for _ in range(NUM_FRAMES):
            f.write(rgb_to_yuv420(texture))

    encode_yuv_to_h264(raw_path, h264_path, W, H, NUM_FRAMES, intra_only=intra_only)
    print(f"  Raw: {raw_path} ({os.path.getsize(raw_path)} bytes)")
    print(f"  H264: {h264_path} ({os.path.getsize(h264_path)} bytes)")
    return name


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--outdir", default="tests/fixtures",
                        help="Output directory for fixtures")
    args = parser.parse_args()

    os.makedirs(args.outdir, exist_ok=True)

    fixtures = []
    # I+P frame versions (IDR every 10 frames)
    fixtures.append(gen_scrolling_texture(args.outdir))
    fixtures.append(gen_bouncing_ball(args.outdir))
    fixtures.append(gen_gradient_pan(args.outdir))

    # Diverse pan directions and speeds — exercises all MV sign/magnitude combos
    # Pan right (positive x): default scrolling_texture already covers this
    # Pan left (negative x): tests negative horizontal MVs
    fixtures.append(gen_pan_direction(args.outdir, "pan_left", dx=-4, dy=0))
    # Pan up (negative y): tests negative vertical MVs
    fixtures.append(gen_pan_direction(args.outdir, "pan_up", dx=0, dy=-3))
    # Pan down (positive y): tests positive vertical-only MVs
    fixtures.append(gen_pan_direction(args.outdir, "pan_down", dx=0, dy=3))
    # Fast diagonal: large MVs, exercises wide MV range and diagonal MC
    fixtures.append(gen_pan_direction(args.outdir, "pan_fast_diag", dx=7, dy=5))
    # Slow sub-pixel pan: fractional-pel MV dominant, tests half/quarter-pel MC
    fixtures.append(gen_pan_direction(args.outdir, "pan_slow", dx=1, dy=1))
    # Static scene: tests zero-MV skip derivation §8.4.1.1
    fixtures.append(gen_static_scene(args.outdir))

    # I-frame-only versions (every frame is IDR)
    fixtures.append(gen_scrolling_texture(args.outdir, name="scrolling_texture_ionly",
                                          intra_only=True))
    fixtures.append(gen_bouncing_ball(args.outdir, name="bouncing_ball_ionly",
                                      intra_only=True))
    fixtures.append(gen_gradient_pan(args.outdir, name="gradient_pan_ionly",
                                     intra_only=True))

    # Compute and display raw source CRCs for each fixture
    print("\n=== Raw source CRC32 values (ground truth) ===")
    for name in fixtures:
        raw_path = os.path.join(args.outdir, f"{name}_raw.yuv")
        crcs = compute_raw_crcs(raw_path, W, H, NUM_FRAMES)
        print(f"\n// {name}: {len(crcs)} frames, {W}x{H}")
        print(f"// Raw source CRC32 (ground truth for PSNR comparison)")
        hex_crcs = ", ".join(f"0x{c:08x}U" for c in crcs[:5])
        print(f"// First 5: {hex_crcs}")

    print(f"\nGenerated {len(fixtures)} synthetic fixtures in {args.outdir}/")
    print("Each has _raw.yuv (source) and .h264 (encoded).")
    print("Decoder quality = PSNR(decoded, raw_source).")


if __name__ == "__main__":
    main()
