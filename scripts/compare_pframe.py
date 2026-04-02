"""Compare decoded P-frame vs raw source to diagnose quality issues.

Usage: python scripts/compare_pframe.py <fixture_name> [frame_idx]
"""
import argparse
import os
import sys
import struct

import numpy as np

W, H = 320, 240


def load_frame(yuv_path, frame_idx, w, h):
    frame_size = w * h + 2 * (w // 2) * (h // 2)
    data = np.fromfile(yuv_path, dtype=np.uint8)
    offset = frame_idx * frame_size
    if offset + frame_size > len(data):
        return None, None, None
    y = data[offset:offset + w * h].reshape(h, w)
    u = data[offset + w * h:offset + w * h + (w // 2) * (h // 2)].reshape(h // 2, w // 2)
    v = data[offset + w * h + (w // 2) * (h // 2):offset + frame_size].reshape(h // 2, w // 2)
    return y, u, v


def decode_fixture(h264_path, w, h, max_frames=5):
    """Decode using our decoder via test harness and dump frames."""
    import subprocess
    # Use ffmpeg to decode for reference comparison
    cmd = ["ffmpeg", "-y", "-i", h264_path, "-pix_fmt", "yuv420p",
           "-frames:v", str(max_frames), "/dev/null"]
    # Actually just return raw data — we need our decoder's output
    # For now, use ffmpeg as a secondary reference
    pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("fixture", help="Fixture name (e.g., gradient_pan)")
    parser.add_argument("frame", type=int, nargs="?", default=1, help="Frame index to check")
    args = parser.parse_args()

    project = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    raw_path = os.path.join(project, "tests", "fixtures", f"{args.fixture}_raw.yuv")

    if not os.path.exists(raw_path):
        print(f"Raw YUV not found: {raw_path}")
        sys.exit(1)

    # Load raw source frame
    y_raw, u_raw, v_raw = load_frame(raw_path, args.frame, W, H)
    if y_raw is None:
        print(f"Frame {args.frame} not in raw YUV")
        sys.exit(1)

    # Load raw source frame 0 (IDR reference)
    y_idr, _, _ = load_frame(raw_path, 0, W, H)

    print(f"Raw source frame {args.frame}: Y mean={y_raw.mean():.1f} range=[{y_raw.min()},{y_raw.max()}]")
    print(f"Raw source frame 0 (IDR): Y mean={y_idr.mean():.1f}")
    print()

    # Compare frame 0 vs frame N in raw source
    diff = np.abs(y_raw.astype(np.int16) - y_idr.astype(np.int16))
    print(f"Raw frame {args.frame} vs frame 0: {np.count_nonzero(diff)}/{W*H} differ, max={diff.max()}")
    print(f"  Mean absolute diff: {diff.mean():.1f}")
    print()

    # For a skip MB (most common in P-frames), the decoder copies from
    # the reference frame with the predicted MV. If the reference is wrong
    # or the MV is wrong, the whole block will be wrong.
    print(f"This tells us how much the frame actually changed from IDR.")
    print(f"If our P-frame PSNR is much worse than expected, the decoder")
    print(f"is either using the wrong reference or applying wrong MVs.")


if __name__ == "__main__":
    main()
