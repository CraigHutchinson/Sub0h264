#!/usr/bin/env python3
"""Compare baseline frame 0 between our decoder and ffmpeg reference.
Reports per-MB match status and first differing pixel.

Usage: python scripts/compare_frame0.py
"""
import subprocess
import sys

def main():
    # Decode reference
    p = subprocess.Popen(
        ["ffmpeg", "-i", "tests/fixtures/baseline_640x480_short.h264",
         "-f", "rawvideo", "-pix_fmt", "yuv420p", "-frames:v", "1",
         "-v", "quiet", "-"],
        stdout=subprocess.PIPE)
    ref = p.stdout.read(640 * 480 * 3 // 2)
    p.wait()

    # Read our output
    try:
        with open("debug_baseline_frame0.yuv", "rb") as f:
            our = f.read()
    except FileNotFoundError:
        print("Run the debug dump test first:")
        print("  build/tests/Release/Sub0h264_Tests.exe -tc=\"*Debug*dump*baseline*\"")
        return

    w, h = 640, 480
    print(f"Frame size: ref={len(ref)} our={len(our)}")

    # Per-MB comparison (luma only)
    correct_mbs = 0
    total_mbs = 0
    first_wrong_mb = None
    for mby in range(h // 16):
        for mbx in range(w // 16):
            match = True
            for dy in range(16):
                for dx in range(16):
                    idx = (mby * 16 + dy) * w + mbx * 16 + dx
                    if ref[idx] != our[idx]:
                        match = False
                        break
                if not match:
                    break
            total_mbs += 1
            if match:
                correct_mbs += 1
            elif first_wrong_mb is None:
                first_wrong_mb = (mbx, mby)
                # Find first differing pixel within this MB
                for dy2 in range(16):
                    for dx2 in range(16):
                        idx2 = (mby*16+dy2)*w + mbx*16+dx2
                        if ref[idx2] != our[idx2]:
                            print(f"First wrong MB: ({mbx},{mby})")
                            print(f"  First diff at MB-local ({dx2},{dy2}): ref={ref[idx2]} our={our[idx2]} delta={our[idx2]-ref[idx2]}")
                            # Show 4x4 block index
                            blk = (dy2//4)*4 + dx2//4
                            print(f"  4x4 block index: {blk}")
                            break
                    else:
                        continue
                    break

    print(f"\nCorrect MBs: {correct_mbs}/{total_mbs} ({100*correct_mbs/total_mbs:.1f}%)")

    # Show correct MB range
    if first_wrong_mb:
        print(f"First wrong MB at: ({first_wrong_mb[0]},{first_wrong_mb[1]})")
        print(f"  MB addr = {first_wrong_mb[1] * (w//16) + first_wrong_mb[0]}")

if __name__ == "__main__":
    main()
