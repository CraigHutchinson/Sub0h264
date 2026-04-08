#!/usr/bin/env python3
"""Generate CAVLC (Baseline) versions of the micro test fixtures.
Same content, different entropy coding — enables direct CABAC vs CAVLC comparison."""
import os, subprocess, shutil

FIXTURES = "tests/fixtures"

# Find all CABAC micro fixtures and create CAVLC versions
import glob

cabac_raws = sorted(glob.glob(os.path.join(FIXTURES, "cabac_*mb_*_raw.yuv")))

generated = []
for raw_path in cabac_raws:
    basename = os.path.basename(raw_path).replace("_raw.yuv", "")
    cavlc_name = basename.replace("cabac_", "cavlc_")

    h264_out = os.path.join(FIXTURES, f"{cavlc_name}.h264")
    raw_out = os.path.join(FIXTURES, f"{cavlc_name}_raw.yuv")

    if os.path.exists(h264_out):
        continue

    # Get dimensions from the cabac h264
    cabac_h264 = os.path.join(FIXTURES, f"{basename}.h264")
    if not os.path.exists(cabac_h264):
        continue

    # Get dims via ffprobe
    r = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0",
                        "-show_entries", "stream=width,height", "-of", "csv=p=0",
                        cabac_h264], capture_output=True, text=True, timeout=5)
    if r.returncode != 0:
        continue
    w, h = r.stdout.strip().split(',')

    # Extract QP from filename if present
    qp = "17"
    if "_qp" in basename:
        qp = basename.split("_qp")[1]

    cmd = ["ffmpeg", "-y", "-f", "rawvideo", "-pix_fmt", "yuv420p",
           "-s", f"{w}x{h}", "-r", "25", "-i", raw_path,
           "-frames:v", "1", "-c:v", "libx264", "-profile:v", "baseline",
           "-preset", "medium", "-qp", qp, "-g", "1", "-bf", "0",
           "-x264-params", f"cabac=0:bframes=0:keyint=1:min-keyint=1",
           h264_out]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(f"  FAIL {cavlc_name}: {r.stderr[-100:]}")
        continue

    # Symlink/copy the raw YUV
    if not os.path.exists(raw_out):
        shutil.copy2(raw_path, raw_out)

    sz = os.path.getsize(h264_out)
    print(f"  {cavlc_name}: {sz:,} bytes")
    generated.append(cavlc_name)

print(f"\nGenerated {len(generated)} CAVLC fixtures")
