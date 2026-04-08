#!/usr/bin/env python3
"""Generate P-frame fixtures at increasing sizes to find where the bug appears."""
import os, subprocess, numpy as np

FIXTURES = "tests/fixtures"

rng = np.random.RandomState(99)

sizes = [(32,32), (64,32), (64,64), (128,64), (128,128), (160,128), (320,240)]

for w, h in sizes:
    name = f"cavlc_pf_hpan4_{w}x{h}"
    yuv = os.path.join(FIXTURES, f"{name}_raw.yuv")
    h264 = os.path.join(FIXTURES, f"{name}.h264")
    if os.path.exists(h264): continue

    # Noisy texture, pan right 4px/frame, 3 frames
    tex = rng.randint(60, 200, (h, w + 16)).astype(np.uint8)
    with open(yuv, "wb") as f:
        for fi in range(3):
            y = tex[:h, fi*4:fi*4+w]
            u = np.full((h//2, w//2), 128, dtype=np.uint8)
            v = np.full((h//2, w//2), 128, dtype=np.uint8)
            f.write(y.tobytes())
            f.write(u.tobytes())
            f.write(v.tobytes())

    cmd = ["ffmpeg", "-y", "-f", "rawvideo", "-pix_fmt", "yuv420p",
           "-s", f"{w}x{h}", "-r", "25", "-i", yuv,
           "-frames:v", "3", "-c:v", "libx264", "-profile:v", "baseline",
           "-preset", "medium", "-qp", "17", "-g", "10", "-bf", "0",
           "-x264-params", "cabac=0:bframes=0:keyint=10:min-keyint=10", h264]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode == 0:
        print(f"  {name}: {os.path.getsize(h264):,} bytes")
    else:
        print(f"  FAIL {name}")
