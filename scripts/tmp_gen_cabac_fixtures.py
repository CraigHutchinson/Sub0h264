#!/usr/bin/env python3
"""Generate CABAC (Main profile) versions of existing CAVLC fixtures.

Uses existing raw YUV sources and re-encodes with CABAC via ffmpeg.
"""
import os
import subprocess
import shutil

W, H = 320, 240
FPS = 25
NUM_FRAMES = 30
QP = 20

fixtures_dir = "tests/fixtures"

# Fixtures that exist as CAVLC with raw YUV but not as CABAC
to_generate = [
    ("bouncing_ball", 30),
    ("bouncing_ball_ionly", 30),
    ("static_scene", 30),
]

for base, nframes in to_generate:
    raw_path = os.path.join(fixtures_dir, f"{base}_raw.yuv")
    if not os.path.exists(raw_path):
        print(f"SKIP {base}: no raw YUV at {raw_path}")
        continue

    cabac_name = f"{base}_cabac"
    h264_path = os.path.join(fixtures_dir, f"{cabac_name}.h264")
    raw_link = os.path.join(fixtures_dir, f"{cabac_name}_raw.yuv")

    if os.path.exists(h264_path):
        print(f"SKIP {cabac_name}: already exists")
        continue

    print(f"Generating {cabac_name} from {raw_path}...")

    intra_only = "ionly" in base
    gop = "1" if intra_only else "10"

    cmd = [
        "ffmpeg", "-y",
        "-f", "rawvideo",
        "-pix_fmt", "yuv420p",
        "-s", f"{W}x{H}",
        "-r", str(FPS),
        "-i", raw_path,
        "-frames:v", str(nframes),
        "-c:v", "libx264",
        "-profile:v", "main",
        "-preset", "medium",
        "-qp", str(QP),
        "-g", gop,
        "-bf", "0",
        "-x264-params", f"cabac=1:no-8x8dct=1:bframes=0:keyint={gop}:min-keyint={gop}",
        h264_path
    ]

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  FAILED: {result.stderr[-200:]}")
        continue

    size = os.path.getsize(h264_path)
    print(f"  Created {h264_path} ({size:,} bytes)")

    # Copy raw YUV reference
    if not os.path.exists(raw_link):
        shutil.copy2(raw_path, raw_link)
        print(f"  Copied raw to {raw_link}")

print("\nDone.")
