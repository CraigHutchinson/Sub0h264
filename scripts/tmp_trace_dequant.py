#!/usr/bin/env python3
"""Trace dequant + IDCT for first block of cabac_flat_main.h264."""

# From spec Table 8-15: LevelScale[qP%6][posClass]
# posClass 0: both-even, 1: mixed, 2: both-odd
DEQUANT_SCALE = [
    [10, 13, 10],  # qp%6=0
    [11, 14, 11],  # qp%6=1
    [13, 16, 13],  # qp%6=2
    [14, 18, 14],  # qp%6=3
    [16, 20, 16],  # qp%6=4
    [18, 23, 18],  # qp%6=5
]

# Position class for 4x4 block (raster order)
# (row,col): class 0 = both even, class 2 = both odd, class 1 = mixed
POS_CLASS = [
    0, 1, 0, 1,  # row 0
    1, 2, 1, 2,  # row 1
    0, 1, 0, 1,  # row 2
    1, 2, 1, 2,  # row 3
]

# Zigzag scan (scan index -> raster position)
ZIGZAG = [0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15]

def dequant_4x4(coeffs_raster, qp):
    """Dequant a 4x4 block per spec §8.5.12.1."""
    qpMod6 = qp % 6
    qpDiv6 = qp // 6
    result = [0] * 16
    for i in range(16):
        if coeffs_raster[i] == 0:
            continue
        pc = POS_CLASS[i]
        ls = DEQUANT_SCALE[qpMod6][pc]
        if qpDiv6 >= 4:
            result[i] = coeffs_raster[i] * ls * (1 << (qpDiv6 - 4))
        else:
            shift = 4 - qpDiv6
            offset = 1 << (shift - 1)
            result[i] = (coeffs_raster[i] * ls + offset) >> shift
    return result

def idct_4x4(d):
    """4x4 inverse DCT per spec §8.5.12.2."""
    # Horizontal transform (rows)
    e = [[0]*4 for _ in range(4)]
    for i in range(4):
        x0 = d[i*4+0] + d[i*4+2]
        x1 = d[i*4+0] - d[i*4+2]
        x2 = (d[i*4+1] >> 1) - d[i*4+3]
        x3 = d[i*4+1] + (d[i*4+3] >> 1)
        e[i][0] = x0 + x3
        e[i][1] = x1 + x2
        e[i][2] = x1 - x2
        e[i][3] = x0 - x3

    # Vertical transform (columns)
    r = [0] * 16
    for j in range(4):
        y0 = e[0][j] + e[2][j]
        y1 = e[0][j] - e[2][j]
        y2 = (e[1][j] >> 1) - e[3][j]
        y3 = e[1][j] + (e[3][j] >> 1)
        r[0*4+j] = (y0 + y3 + 32) >> 6
        r[1*4+j] = (y1 + y2 + 32) >> 6
        r[2*4+j] = (y1 - y2 + 32) >> 6
        r[3*4+j] = (y0 - y3 + 32) >> 6
    return r

# From Python trace of cabac_flat_main.h264:
# blk_scan0: cbf=0 (no coeff)
# blk_scan1: cbf=1 numSig=10 lastSig=14 coeffs=[16,1,-7,-10,15,2,-3,-3,2,-1]
# The coefficients are in SCAN order, positions where they're non-zero

# Scan order coefficients (15 positions for scan indices 0-14)
scan_coeffs = [0] * 16
# From the trace: the 10 nonzero coefficients at their scan positions
# Need to reconstruct the full scan array
# trace says numSig=10, lastSig=14, coeffs=[16,1,-7,-10,15,2,-3,-3,2,-1]
# These are listed from the last significant backwards in the trace output
# Actually the trace prints them in forward scan order

# Let me re-read: "coeffs=[16,1,-7,-10,15,2,-3,-3,2,-1]"
# This is the 10 nonzero values. From the sig map trace:
# sig[0]=1, sig[1]=0, sig[2]=0, sig[3]=1, sig[4]=0, sig[5]=1
# lastSig=14 means last nonzero is at scan position 14

# Let me just use what the Python trace decoded for all positions
# For scan 1 (the first coded block): sig map from the trace shows
# positions 0,3,5 and more are significant

# Actually let me just check what blk_scan0 does (first block, cbf=0)
print("=== Block scan 0: cbf=0 (no coefficients) ===")
print("This block gets DC prediction only (128 for first block)")
print("Output: 128 everywhere")
print()

# For the first CODED block (scan 1), let me check what scan position
# maps to what raster position
print("=== Block scan 1 coefficients analysis ===")
# From trace: sig[0]=1 last=no, sig[3]=1 last=no, sig[5]=1 last=no
# The 10 values [16,1,-7,-10,15,2,-3,-3,2,-1] at their scan positions

# The scan order for I_4x4 ctxBlockCat=2 uses scan positions 0..15
# Scan 1 is the second 4x4 block in spec scan order
# Spec scan order §6.4.3: block 0=(0,0), block 1=(4,0), ...

# For QP=17: qpMod6=5, qpDiv6=2
qp = 17
print(f"QP={qp}, qpMod6={qp%6}, qpDiv6={qp//6}")
print(f"LevelScale: class0={DEQUANT_SCALE[qp%6][0]}, class1={DEQUANT_SCALE[qp%6][1]}, class2={DEQUANT_SCALE[qp%6][2]}")

# Test with DC=16 only
raster = [0] * 16
raster[0] = 16  # DC position (0,0) = class 0
dequant = dequant_4x4(raster, qp)
print(f"\nDC only: coeff[0]={raster[0]}")
print(f"  dequant[0] = ({raster[0]} * {DEQUANT_SCALE[qp%6][0]} + {1<<(4-qp//6-1)}) >> {4-qp//6}")
print(f"  = ({raster[0] * DEQUANT_SCALE[qp%6][0]} + {1<<(4-qp//6-1)}) >> {4-qp//6}")
val = raster[0] * DEQUANT_SCALE[qp%6][0]
shift = 4 - qp//6
offset = 1 << (shift-1)
print(f"  = ({val} + {offset}) >> {shift} = {(val+offset)>>shift}")
print(f"  dequant result: {dequant}")

idct = idct_4x4(dequant)
print(f"  IDCT result: {idct}")
print(f"  pred=128, output: {[128 + v for v in idct]}")
