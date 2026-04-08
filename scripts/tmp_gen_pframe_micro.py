#!/usr/bin/env python3
"""Generate minimal P-frame test fixtures to isolate inter prediction bugs.

Motion patterns:
1. Static (zero MV) — 2 frames, IDR+P with identical content. P-skip only.
2. Horizontal pan right by 1 pixel — simplest non-zero motion
3. Horizontal pan right by 4 pixels — full-pel motion, no sub-pel
4. Horizontal pan right by 16 pixels — full MB motion
5. Vertical pan down by 1 pixel
6. Diagonal motion (1,1)
7. Large motion (8,4) — quarter-pel interpolation
8. Single MB different — only one MB changes between frames
"""
import os, subprocess, numpy as np

FIXTURES = "tests/fixtures"

def gen_yuv_frames(path, w, h, frames_fn):
    """Write multi-frame raw YUV."""
    with open(path, 'wb') as f:
        for frame_idx in range(frames_fn(None)):
            y, u, v = frames_fn(frame_idx)
            f.write(y.astype(np.uint8).tobytes())
            f.write(u.astype(np.uint8).tobytes())
            f.write(v.astype(np.uint8).tobytes())

def encode(yuv, h264, w, h, nframes, profile="baseline"):
    cabac = "0" if profile == "baseline" else "1"
    cmd = ["ffmpeg", "-y", "-f", "rawvideo", "-pix_fmt", "yuv420p",
           "-s", f"{w}x{h}", "-r", "25", "-i", yuv,
           "-frames:v", str(nframes), "-c:v", "libx264", "-profile:v", profile,
           "-preset", "medium", "-qp", "17", "-g", "10", "-bf", "0",
           "-x264-params", f"cabac={cabac}:bframes=0:keyint=10:min-keyint=10",
           h264]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode == 0

W, H = 32, 32  # 4 MBs — small enough to trace, big enough for motion
NFRAMES = 3  # IDR + 2 P-frames

rng = np.random.RandomState(99)
base_texture = rng.randint(60, 200, (H + 32, W + 32)).astype(np.uint8)  # larger for panning

generated = []

def make(name, w, h, nframes, frame_fn):
    yuv = os.path.join(FIXTURES, f"cavlc_pf_{name}_raw.yuv")
    h264 = os.path.join(FIXTURES, f"cavlc_pf_{name}.h264")
    if os.path.exists(h264): return

    with open(yuv, 'wb') as f:
        for fi in range(nframes):
            y, u, v = frame_fn(fi)
            f.write(y.astype(np.uint8).tobytes())
            f.write(u.astype(np.uint8).tobytes())
            f.write(v.astype(np.uint8).tobytes())

    if encode(yuv, h264, w, h, nframes):
        print(f"  {name}: {os.path.getsize(h264):,} bytes")
        generated.append(name)
    else:
        print(f"  FAIL {name}")

def uv_flat(h, w):
    return np.full((h//2, w//2), 128, dtype=np.uint8), np.full((h//2, w//2), 128, dtype=np.uint8)

# 1. Static — identical frames, should be P-skip only
print("=== Static (zero motion) ===")
def static_frames(fi):
    y = base_texture[:H, :W].copy()
    u, v = uv_flat(H, W)
    return y, u, v
make("static", W, H, NFRAMES, static_frames)

# 2. Horizontal pan right +1 pixel per frame
print("=== Horizontal pan +1 px/frame ===")
def hpan1_frames(fi):
    y = base_texture[:H, fi:fi+W].copy()
    u, v = uv_flat(H, W)
    return y, u, v
make("hpan1", W, H, NFRAMES, hpan1_frames)

# 3. Horizontal pan right +4 pixels per frame (full-pel)
print("=== Horizontal pan +4 px/frame ===")
def hpan4_frames(fi):
    y = base_texture[:H, fi*4:fi*4+W].copy()
    u, v = uv_flat(H, W)
    return y, u, v
make("hpan4", W, H, NFRAMES, hpan4_frames)

# 4. Horizontal pan right +16 pixels per frame (full MB)
print("=== Horizontal pan +16 px/frame ===")
def hpan16_frames(fi):
    y = base_texture[:H, fi*16:fi*16+W].copy()
    u, v = uv_flat(H, W)
    return y, u, v
make("hpan16", W, H, NFRAMES, hpan16_frames)

# 5. Vertical pan down +1 pixel per frame
print("=== Vertical pan +1 px/frame ===")
def vpan1_frames(fi):
    y = base_texture[fi:fi+H, :W].copy()
    u, v = uv_flat(H, W)
    return y, u, v
make("vpan1", W, H, NFRAMES, vpan1_frames)

# 6. Diagonal motion (1,1)
print("=== Diagonal pan (1,1) ===")
def diagpan_frames(fi):
    y = base_texture[fi:fi+H, fi:fi+W].copy()
    u, v = uv_flat(H, W)
    return y, u, v
make("diag11", W, H, NFRAMES, diagpan_frames)

# 7. Large motion (8,4)
print("=== Large motion (8,4) per frame ===")
def largepan_frames(fi):
    y = base_texture[fi*4:fi*4+H, fi*8:fi*8+W].copy()
    u, v = uv_flat(H, W)
    return y, u, v
make("large84", W, H, NFRAMES, largepan_frames)

# 8. Single MB changes — only bottom-right MB differs between frames
print("=== Single MB change (bottom-right) ===")
def single_mb_frames(fi):
    y = base_texture[:H, :W].copy()
    if fi > 0:
        # Shift bottom-right 16x16 block by fi pixels
        y[16:32, 16:32] = base_texture[16+fi:32+fi, 16:32]
    u, v = uv_flat(H, W)
    return y, u, v
make("single_mb", W, H, NFRAMES, single_mb_frames)

# 9. Zero-motion with residual — uniform shift of all pixels
print("=== Zero MV with residual (brightness change) ===")
def brightness_frames(fi):
    y = np.clip(base_texture[:H, :W].astype(int) + fi * 10, 0, 255).astype(np.uint8)
    u, v = uv_flat(H, W)
    return y, u, v
make("brightness", W, H, NFRAMES, brightness_frames)

print(f"\nGenerated {len(generated)} P-frame fixtures")
