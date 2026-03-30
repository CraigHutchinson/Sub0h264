#!/usr/bin/env python3
"""Generate reference frames and visual diffs for CRC-failing test reports.

Scans build/test_reports/ for frame_*_FAIL/ directories, decodes the
corresponding reference frame from the H.264 fixture via ffmpeg, and
generates diff images.

Usage:
    python scripts/gen_frame_diffs.py [--report-dir build/test_reports]

Outputs per failure directory:
    reference_gray.pgm  — ffmpeg-decoded reference (grayscale)
    reference.ppm       — ffmpeg-decoded reference (RGB)
    diff_linear.pgm     — |actual - reference| (linear scale)
    diff_log.pgm        — |actual - reference| (log scale, reveals small diffs)
    report.txt          — updated with PSNR and diff statistics

SPDX-License-Identifier: MIT
"""
import argparse
import glob
import math
import os
import struct
import subprocess
import sys


def read_pgm(path):
    """Read a P5 (grayscale) PGM/PPM file, return (width, height, data)."""
    with open(path, "rb") as f:
        magic = f.readline().strip()
        if magic not in (b"P5", b"P6"):
            return None, None, None
        # Skip comments
        line = f.readline()
        while line.startswith(b"#"):
            line = f.readline()
        w, h = map(int, line.split())
        maxval = int(f.readline().strip())
        channels = 3 if magic == b"P6" else 1
        data = f.read(w * h * channels)
    return w, h, data


def decode_frame_ffmpeg(fixture_path, frame_idx, width, height):
    """Decode a specific frame from an H.264 file using ffmpeg. Returns raw I420 bytes."""
    cmd = [
        "ffmpeg", "-i", fixture_path,
        "-f", "rawvideo", "-pix_fmt", "yuv420p",
        "-vf", f"select=eq(n\\,{frame_idx})",
        "-vsync", "vfr",
        "-frames:v", "1",
        "-v", "quiet", "-"
    ]
    try:
        result = subprocess.run(cmd, capture_output=True, timeout=30)
        return result.stdout
    except Exception as e:
        print(f"  ffmpeg failed: {e}", file=sys.stderr)
        return None


def yuv_to_gray_pgm(yuv_data, w, h):
    """Extract Y plane from I420 data as PGM bytes (P5 format)."""
    header = f"P5\n{w} {h}\n255\n".encode()
    return header + yuv_data[:w * h]


def yuv_to_rgb_ppm(yuv_data, w, h):
    """Convert I420 to RGB PPM bytes (P6 format) using BT.601."""
    y_plane = yuv_data[:w * h]
    u_plane = yuv_data[w * h:w * h + (w // 2) * (h // 2)]
    v_plane = yuv_data[w * h + (w // 2) * (h // 2):]

    rgb = bytearray(w * h * 3)
    for row in range(h):
        for col in range(w):
            y = y_plane[row * w + col]
            u = u_plane[(row // 2) * (w // 2) + col // 2] - 128
            v = v_plane[(row // 2) * (w // 2) + col // 2] - 128
            r = max(0, min(255, int(y + 1.402 * v)))
            g = max(0, min(255, int(y - 0.344 * u - 0.714 * v)))
            b = max(0, min(255, int(y + 1.772 * u)))
            idx = (row * w + col) * 3
            rgb[idx] = r
            rgb[idx + 1] = g
            rgb[idx + 2] = b

    header = f"P6\n{w} {h}\n255\n".encode()
    return header + bytes(rgb)


def compute_diff_pgm(actual_gray, ref_gray, w, h, log_scale=False):
    """Compute absolute difference image as PGM."""
    header = f"P5\n{w} {h}\n255\n".encode()
    diff = bytearray(w * h)
    for i in range(w * h):
        d = abs(actual_gray[i] - ref_gray[i])
        if log_scale:
            diff[i] = 0 if d == 0 else min(255, int(255 * math.log2(1 + d) / 8))
        else:
            diff[i] = min(255, d)
    return header + bytes(diff)


def compute_psnr(actual_gray, ref_gray, w, h):
    """Compute PSNR in dB between two grayscale images."""
    sse = sum((actual_gray[i] - ref_gray[i]) ** 2 for i in range(w * h))
    if sse == 0:
        return 999.0
    mse = sse / (w * h)
    return 10 * math.log10(255 * 255 / mse)


def process_failure_dir(fail_dir, fixtures_dir):
    """Process one frame_NNN_FAIL directory."""
    # Parse stream name and frame index from path
    # e.g., test_reports/baseline_640x480_short/frame_000_FAIL
    parts = os.path.normpath(fail_dir).split(os.sep)
    stream_name = parts[-2]
    frame_part = parts[-1]  # frame_000_FAIL
    frame_idx = int(frame_part.split("_")[1])

    fixture_file = os.path.join(fixtures_dir, stream_name + ".h264")
    if not os.path.exists(fixture_file):
        print(f"  Fixture not found: {fixture_file}")
        return

    actual_pgm = os.path.join(fail_dir, "actual_gray.pgm")
    if not os.path.exists(actual_pgm):
        print(f"  No actual_gray.pgm in {fail_dir}")
        return

    w, h, actual_data = read_pgm(actual_pgm)
    if actual_data is None:
        print(f"  Failed to read {actual_pgm}")
        return

    print(f"  Decoding reference frame {frame_idx} from {fixture_file} ({w}x{h})...")
    yuv = decode_frame_ffmpeg(fixture_file, frame_idx, w, h)
    if not yuv or len(yuv) < w * h:
        print(f"  Failed to decode reference frame")
        return

    ref_gray = yuv[:w * h]

    # Write reference images
    with open(os.path.join(fail_dir, "reference_gray.pgm"), "wb") as f:
        f.write(yuv_to_gray_pgm(yuv, w, h))
    with open(os.path.join(fail_dir, "reference.ppm"), "wb") as f:
        f.write(yuv_to_rgb_ppm(yuv, w, h))

    # Generate diff images
    diff_linear = compute_diff_pgm(actual_data, ref_gray, w, h, log_scale=False)
    diff_log = compute_diff_pgm(actual_data, ref_gray, w, h, log_scale=True)
    with open(os.path.join(fail_dir, "diff_linear.pgm"), "wb") as f:
        f.write(diff_linear)
    with open(os.path.join(fail_dir, "diff_log.pgm"), "wb") as f:
        f.write(diff_log)

    # Compute statistics
    psnr = compute_psnr(actual_data, ref_gray, w, h)
    diff_count = sum(1 for i in range(w * h) if actual_data[i] != ref_gray[i])
    max_diff = max(abs(actual_data[i] - ref_gray[i]) for i in range(w * h))
    diff_pct = 100 * diff_count / (w * h)

    # Update report
    report_path = os.path.join(fail_dir, "report.txt")
    with open(report_path, "a") as f:
        f.write(f"\n--- Post-processed by gen_frame_diffs.py ---\n")
        f.write(f"PSNR (Y):       {psnr:.2f} dB\n")
        f.write(f"Diff pixels:    {diff_count} ({diff_pct:.1f}%)\n")
        f.write(f"Max |diff|:     {max_diff}\n")
        if psnr >= 40:
            f.write(f"Verdict:        WARN (visually identical, PSNR {psnr:.1f} dB)\n")
        elif psnr >= 20:
            f.write(f"Verdict:        FAIL (minor differences, PSNR {psnr:.1f} dB)\n")
        else:
            f.write(f"Verdict:        FAIL (significant errors, PSNR {psnr:.1f} dB)\n")

    print(f"  PSNR={psnr:.1f} dB, {diff_count} diffs ({diff_pct:.1f}%), max|diff|={max_diff}")


def main():
    parser = argparse.ArgumentParser(description="Generate frame diffs for CRC failures")
    parser.add_argument("--report-dir", default="build/test_reports",
                        help="Directory containing test reports")
    parser.add_argument("--fixtures-dir", default="tests/fixtures",
                        help="Directory containing H.264 test fixtures")
    args = parser.parse_args()

    fail_dirs = sorted(glob.glob(os.path.join(args.report_dir, "*", "frame_*_FAIL")))
    if not fail_dirs:
        print(f"No failure reports found in {args.report_dir}/")
        return

    print(f"Found {len(fail_dirs)} failure report(s)")
    for d in fail_dirs:
        print(f"\nProcessing {d}...")
        process_failure_dir(d, args.fixtures_dir)

    print(f"\nDone. View results in {args.report_dir}/")


if __name__ == "__main__":
    main()
