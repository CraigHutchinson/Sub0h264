#!/usr/bin/env python3
"""Per-pixel diff of hbands fixture: our CAVLC vs ffmpeg vs raw."""
import subprocess, os

FIXTURES = "tests/fixtures"
W, H = 32, 32

h264 = os.path.join(FIXTURES, "cavlc_4mb_hbands.h264")
raw = os.path.join(FIXTURES, "cavlc_4mb_hbands_raw.yuv")

# ffmpeg decode
r = subprocess.run(["ffmpeg", "-i", h264, "-vframes", "1", "-pix_fmt", "yuv420p",
                    "-f", "rawvideo", "pipe:"], capture_output=True, timeout=10)
ff_y = r.stdout[:W*H]

# Raw
raw_y = open(raw, 'rb').read()[:W*H]

print(f"=== Raw source (first 8 rows, 32 cols) ===")
for r in range(8):
    vals = [raw_y[r*W+c] for c in range(W)]
    print(f"  row {r:2d}: {' '.join(f'{v:3d}' for v in vals)}")

print(f"\n=== ffmpeg output (first 8 rows) ===")
for r in range(8):
    vals = [ff_y[r*W+c] for c in range(W)]
    print(f"  row {r:2d}: {' '.join(f'{v:3d}' for v in vals)}")

print(f"\n=== ffmpeg vs raw diff (first 8 rows) ===")
for r in range(8):
    diffs = [ff_y[r*W+c] - raw_y[r*W+c] for c in range(W)]
    print(f"  row {r:2d}: {' '.join(f'{d:+3d}' for d in diffs)}")

# Overall stats
diffs_all = [abs(ff_y[i] - raw_y[i]) for i in range(W*H)]
print(f"\nffmpeg max diff: {max(diffs_all)}, mean diff: {sum(diffs_all)/len(diffs_all):.2f}")
