#!/usr/bin/env python3
"""Full trace: decode block scan 1 from cabac_flat_main, dequant, IDCT, add prediction."""

ZIGZAG = [0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15]

DEQUANT_SCALE = [
    [10, 13, 10], [11, 14, 11], [13, 16, 13],
    [14, 18, 14], [16, 20, 16], [18, 23, 18],
]

POS_CLASS = [0,1,0,1, 1,2,1,2, 0,1,0,1, 1,2,1,2]

def dequant(coeffs, qp):
    qpMod6 = qp % 6
    qpDiv6 = qp // 6
    result = [0]*16
    for i in range(16):
        if coeffs[i] == 0: continue
        pc = POS_CLASS[i]
        ls = DEQUANT_SCALE[qpMod6][pc]
        if qpDiv6 >= 4:
            result[i] = coeffs[i] * ls * (1 << (qpDiv6-4))
        else:
            shift = 4 - qpDiv6
            offset = 1 << (shift-1)
            result[i] = (coeffs[i] * ls + offset) >> shift
    return result

def idct_4x4(d):
    # Horizontal
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
    # Vertical
    r = [0]*16
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

def clip(v): return max(0, min(255, v))

# From Python trace of cabac_flat_main.h264:
# blk_scan1: cbf=1 numSig=10 lastSig=14 coeffs=[16,1,-7,-10,15,2,-3,-3,2,-1]
# These are 10 nonzero values. The significant map positions need to be reconstructed.
# sig[0]=1, sig[3]=1, sig[5]=1 from the verbose trace

# Actually the trace output lists coefficients in scan-order positions.
# Let me reconstruct: we have 10 nonzero at positions within 0-14 (lastSig=14)
# The trace says "coeffs=[16,1,-7,-10,15,2,-3,-3,2,-1]" which lists the 10 values
# in order from first significant to last.

# From the sig trace for this block:
# sig[0]=1 -> coeff at scan 0
# sig[1]=0
# sig[2]=0
# sig[3]=1 -> coeff at scan 3
# sig[4]=0
# sig[5]=1 -> coeff at scan 5
# After last verbose line, need more trace. But we have 10 coeffs, lastSig=14.

# Let me just use the coefficients as reported: 10 values at some scan positions.
# The values in order are [16,1,-7,-10,15,2,-3,-3,2,-1]
# We know positions 0,3,5 are significant from the trace
# Let me look at the trace more carefully...

# From the full trace output:
# sig[0]: sig=1 -> coeff position 0
# sig[1]: sig=0
# sig[2]: sig=0
# sig[3]: sig=1 -> coeff position 3
# sig[4]: sig=0
# sig[5]: sig=1 -> coeff position 5
# Then positions 6-13 we don't have verbose output for
# lastSig=14, and total 10 significant
# So 7 more positions must be significant in 6-14

# This is hard to reconstruct without full sig map. Let me instead
# check if block scan 0 (first block, cbf=0) is correct.

print("=== Block scan 0 (cbf=0) ===")
print("Prediction: DC mode, no neighbors available -> pred=128")
print("Residual: all zeros (cbf=0)")
print("Output: all 128")
print()

# For scan block 0 at position (0,0), DC prediction with no neighbors = 128
# If raw=121, residual should be -7 per pixel
# cbf=0 means NO residual decoded -> output stays at 128
# This is WRONG by 7 pixels

# But the question is: is cbf=0 what the bitstream actually encodes?
# For a flat gray image, if the encoder uses I_4x4 with DC mode,
# and the prediction is 128, the residual is -7 everywhere.
# After forward DCT of a constant -7 block:
# DC = -7*4 = -28 (DCT of constant value = value * 4)
# Wait, the H.264 forward transform of a constant block C produces:
# DC position = C (for the integer DCT), actually let me compute it properly.

# H.264 forward integer DCT of a constant block with value v:
# All elements are v. The transform matrix Cf = [[1,1,1,1],[2,1,-1,-2],[1,-1,-1,1],[1,-2,2,-1]]
# Row transform: first column = sum = 4v, others = 0
# Column transform: first row of first column = 4*(4v) = 16v
# So DC = 16*v where v is the pixel value per block... no wait
# The forward DCT operates on the RESIDUAL, not the pixel values

# Residual = pixel - prediction = 121 - 128 = -7 (constant)
# Forward DCT of [-7, -7, -7, -7; -7, -7, -7, -7; -7, -7, -7, -7; -7, -7, -7, -7]
# DC = 4 * sum_of_first_row = 4*(-28) = -112? No...
# Actually H.264 integer DCT: Y = Cf * X * Cf^T
# For constant X = -7:
# Cf * X = [[-28], [0], [0], [0]] (first column)
# Then * Cf^T = [[-28*1, -28*1, -28*1, -28*1], [0,0,0,0], [0,0,0,0], [0,0,0,0]]
# Wait, that gives -28 at DC, not -112. Let me recalculate.

# Cf = [[1,1,1,1],[2,1,-1,-2],[1,-1,-1,1],[1,-2,2,-1]]
# For row 0 of constant -7: [-7,-7,-7,-7]
# Cf[0] * row = 1*(-7) + 1*(-7) + 1*(-7) + 1*(-7) = -28
# Cf[1] * row = 2*(-7) + 1*(-7) + (-1)*(-7) + (-2)*(-7) = -14-7+7+14 = 0
# etc
# So after row transform, intermediate = [[-28,0,0,0],[-28,0,0,0],[-28,0,0,0],[-28,0,0,0]]
# Column transform on first column: [-28,-28,-28,-28]
# Cf[0] * col = -28*4 = -112
# So DC = -112 in the integer DCT domain

# After quantization at QP=17 (qpMod6=5, qpDiv6=2):
# Quant formula: level = (|coeff| * MF + f) >> qbits
# MF for DC position (class 0) at qpMod6=5: 1/LevelScale = ...
# Actually the ENCODER quantizes with MF, but we care about what level the encoder chose.

# If DC = -112, and we want to verify dequant(-112/scale) gives back ~-112:
# dequant(level, QP=17): d = (level * 18 + 2) >> 2
# For d = -112: level * 18 + 2 = -448 + k, level = (-448-2)/18 ≈ -25
# So level should be about -25

# But our CABAC trace decoded level=16 for position 0!
# And there are 10 nonzero coefficients, not 1!

# This means the CABAC is decoding the wrong bitstream data.
# OR the sig map positions don't match what I think.

# Actually wait -- I need to recheck. The Python trace says:
# blk_scan1 (not blk_scan0).
# blk_scan0 has cbf=0.
# blk_scan1 is the SECOND block in scan order, at position (4,0).
# For the first block (0,0), cbf=0 means no residual.
# But for pixel=121 with prediction=128, we NEED residual=-7.

# Unless the prediction is NOT 128 for the first block.
# Let me check: for I_4x4, the prediction mode for block 0 is prev_flag=1 (MPM).
# MPM = min(leftMode, topMode). With no left and no top, both default to DC (mode 2).
# MPM = min(2,2) = 2 = DC.
# DC prediction with no neighbors = (1 << (bitDepth-1)) = 128.

# So prediction=128, raw=121, cbf=0 -> output=128.
# This is DEFINITELY wrong. The encoder MUST have encoded non-zero residual.
# Either cbf=0 is a wrong decode, or the bitstream really has cbf=0.

# Given only 49 bytes of CABAC data for a 320x240 frame,
# the encoder is using very aggressive compression.
# For flat gray, I_4x4 DC prediction=128 is close enough at QP=17
# that the encoder might quantize residual=-7 to zero!

# Let's check: forward quantize of DC=-112 at QP=17
# Quant: level = (|DC| * MF + f) >> qbits
# For 4x4 transform: qbits = 15 + qP/6 = 15+2 = 17
# MF for DC position at qpMod6=5: MF = 2^(qbits) / (LevelScale*16)
# Actually H.264 uses: level = (|Zij| * MF + f) >> qbits
# where MF and qbits depend on QP

# At QP=17: the quantization step size is roughly:
# Qstep ≈ 2^(QP/6) * levelScale / 16 ≈ 4 * 18 / 16 = 4.5
# Residual DC after DCT = -112
# Quantized: |-112| / Qstep ≈ 112 / 4.5 ≈ 24.9 -> level ≈ 25

# So the encoder SHOULD encode level=-25 (non-zero). But our trace shows cbf=0!
# This confirms the cbf decode is wrong OR the bitstream position is off.

print("=== Analysis ===")
print("For flat gray (raw=121, pred=128), residual=-7")
print("Forward DCT of constant -7 block: DC=-112")
print("At QP=17: quantized level ≈ -25 (definitely non-zero)")
print("But our CABAC trace decodes cbf=0 for block scan 0!")
print("This means either:")
print("  1. The CABAC engine is at the wrong bitstream position for cbf decode")
print("  2. The cbf context is wrong (using wrong neighbor data)")
print("  3. The block scan order is wrong (decoding blocks in wrong order)")
print()
print("Key clue: 49 bytes of CABAC data for a 320x240 I_4x4 frame is")
print("extremely small. This suggests the encoder made specific choices")
print("about which blocks have residual and which don't.")
print()

# Actually -- maybe the prediction is NOT DC mode 2!
# The trace shows "blk0: prev_flag=1 (MPM)" which means the prediction
# mode IS the most probable mode. But MPM could be any mode depending
# on neighbors. For the first block with no neighbors, MPM = DC.
# But what if the encoder chose a different mode?

# In CABAC, prev_intra4x4_pred_mode_flag=1 means "use MPM".
# MPM for first block = min(leftMode_default, topMode_default) = min(2,2) = 2 = DC.
# DC prediction with no neighbors = 128. This is correct.

# So the CABAC correctly decodes the mode, but cbf=0 means no residual.
# This COULD be correct if the encoder decided the quantized residual is zero.
# But -112 / 4.5 ≈ -25, which is far from zero.

# UNLESS the encoder used I_16x16 instead of I_4x4.
# I_16x16 DC prediction of 128 has residual -7 everywhere.
# The 4x4 Hadamard transform of constant -7:
# DC-of-DC = -7 * 16 = -112
# After quantization: level ≈ -25 for the DC-of-DC
# But then only 1 coefficient needed (all AC = 0)
# 1 MB needs: mb_type(~1 bin) + DC level(~5 bins) ≈ 6 bins
# 300 MBs * 6 bins ≈ 1800 bins ≈ ~600 bits ≈ 75 bytes

# But we only have 49 bytes! Even I_16x16 might not fit...
# Unless many MBs have ALL-zero residual (very common in flat content)

print("Alternative: if encoder used I_16x16:")
print("  DC-of-DC = -7*16 = -112")
print("  At QP=17, quantized level ≈ -25 for DC position")
print("  Most MBs: mb_type + zero residual ≈ 2-3 bins")
print("  300 MBs * 3 bins ≈ 900 bins ≈ ~300 bits = 37 bytes")
print("  This fits in 49 bytes of CABAC data")
