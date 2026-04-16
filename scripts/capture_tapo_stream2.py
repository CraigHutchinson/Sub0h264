#!/usr/bin/env python3
"""Capture H.264 stream from Tapo C110 camera (stream2, low-res).

Captures 5 seconds of raw Annex B H.264 via RTSP for use as a real-world
benchmark fixture. The captured stream represents the actual production
target for the ESP32-P4 decoder.

Credentials are read from environment variables (NEVER hardcoded):
  TAPO_RTSP_HOST  — camera IP (default 192.168.1.112)
  TAPO_RTSP_USER  — ONVIF/RTSP username (default NestCam)
  TAPO_RTSP_PASS  — ONVIF/RTSP password (required, no default)

Usage:
    set TAPO_RTSP_PASS=<password>
    python scripts/capture_tapo_stream2.py [--outdir tests/fixtures]

Output:
    tests/fixtures/tapo_c110_stream2_baseline.h264

SPDX-License-Identifier: MIT
"""
import argparse
import os
import subprocess
import sys


def capture(host: str, user: str, password: str, output: str,
            duration: float = 5.0) -> bool:
    """Capture RTSP stream to raw H.264 Annex B file."""
    url = f"rtsp://{user}:{password}@{host}:554/stream2"
    cmd = [
        "ffmpeg", "-y",
        "-rtsp_transport", "tcp",
        "-i", url,
        "-t", str(duration),
        "-c", "copy",
        "-f", "h264",
        output,
    ]
    print(f"Capturing {duration}s from {host} stream2...")
    print(f"  URL: rtsp://{user}:****@{host}:554/stream2")
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    if result.returncode != 0:
        print(f"  ffmpeg failed (exit {result.returncode}):")
        print(f"  {result.stderr[-500:]}")
        return False
    return True


def validate(path: str) -> dict:
    """Validate the captured H.264 file and report stats."""
    size = os.path.getsize(path)
    print(f"  Output: {path} ({size} bytes)")

    # Probe with ffprobe
    cmd = [
        "ffprobe", "-v", "error",
        "-select_streams", "v:0",
        "-show_entries", "stream=width,height,nb_frames,codec_name,profile",
        "-of", "csv=p=0",
        path,
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  ffprobe failed: {result.stderr}")
        return {}

    parts = result.stdout.strip().split(",")
    info = {}
    if len(parts) >= 2:
        info["codec"] = parts[0]
        info["width"] = int(parts[1]) if parts[1].isdigit() else 0
        info["height"] = int(parts[2]) if len(parts) > 2 and parts[2].isdigit() else 0
        info["profile"] = parts[3] if len(parts) > 3 else "unknown"
        info["frames"] = parts[4] if len(parts) > 4 else "N/A"
    info["size"] = size

    print(f"  Resolution: {info.get('width', '?')}x{info.get('height', '?')}")
    print(f"  Profile: {info.get('profile', '?')}")
    print(f"  Frames: {info.get('frames', '?')}")
    print(f"  Size: {size} bytes ({size/1024:.1f} KB)")
    return info


def main():
    parser = argparse.ArgumentParser(description="Capture Tapo C110 stream2")
    parser.add_argument("--outdir", default="tests/fixtures",
                        help="Output directory")
    parser.add_argument("--duration", type=float, default=5.0,
                        help="Capture duration in seconds")
    args = parser.parse_args()

    host = os.environ.get("TAPO_RTSP_HOST", "192.168.1.112")
    user = os.environ.get("TAPO_RTSP_USER", "NestCam")
    password = os.environ.get("TAPO_RTSP_PASS")

    if not password:
        print("ERROR: TAPO_RTSP_PASS environment variable is required.")
        print("  set TAPO_RTSP_PASS=<your_password>")
        sys.exit(1)

    output = os.path.join(args.outdir, "tapo_c110_stream2_baseline.h264")
    os.makedirs(args.outdir, exist_ok=True)

    if not capture(host, user, password, output, args.duration):
        sys.exit(1)

    info = validate(output)
    if not info:
        print("ERROR: Captured file failed validation.")
        sys.exit(1)

    print(f"\nCapture successful. Fixture ready at: {output}")


if __name__ == "__main__":
    main()
