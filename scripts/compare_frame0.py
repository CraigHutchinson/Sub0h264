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

    if first_wrong_mb:
        print(f"First wrong MB at: ({first_wrong_mb[0]},{first_wrong_mb[1]})")
        print(f"  MB addr = {first_wrong_mb[1] * (w//16) + first_wrong_mb[0]}")

    # Detailed 4x4 block comparison for MB 9
    print(f"\n=== MB(9,0) 4x4 block comparison ===")
    mbx, mby = 9, 0
    for blk in range(16):
        bx = (blk & 3) * 4
        by = (blk >> 2) * 4
        diffs = 0
        max_d = 0
        for dy in range(4):
            for dx in range(4):
                idx = (mby*16+by+dy)*w + mbx*16+bx+dx
                d = abs(ref[idx] - our[idx])
                if d > 0: diffs += 1
                max_d = max(max_d, d)
        status = "OK" if diffs == 0 else f"DIFF ({diffs} px, max|d|={max_d})"
        # Group mapping
        grp = (1 if by >= 8 else 0)*2 + (1 if bx >= 8 else 0)
        coded = "coded" if (8 >> grp) & 1 else "pred-only"
        print(f"  blk{blk:2d} ({bx:2d},{by:2d}) grp{grp} {coded:9s}: {status}")
        # Show pixel details for blocks with large errors
        if max_d >= 10:
            print(f"    ref row0: {[ref[(mby*16+by+0)*w + mbx*16+bx+dx] for dx in range(4)]}")
            print(f"    our row0: {[our[(mby*16+by+0)*w + mbx*16+bx+dx] for dx in range(4)]}")
            print(f"    ref row3: {[ref[(mby*16+by+3)*w + mbx*16+bx+dx] for dx in range(4)]}")
            print(f"    our row3: {[our[(mby*16+by+3)*w + mbx*16+bx+dx] for dx in range(4)]}")

if __name__ == "__main__":
    main()
