/** Spec-only Dequantization and Inverse Transform
 *
 *  ITU-T H.264 Section 8.5.12   (Scaling and transform)
 *  ITU-T H.264 Section 8.5.12.1 (Scaling -- inverse quantization)
 *  ITU-T H.264 Section 8.5.12.2 (4x4 inverse transform)
 *  ITU-T H.264 Section 8.5.11   (Inverse scanning -- zigzag)
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef CROG_SUB0H264_SPEC_REF_TRANSFORM_HPP
#define CROG_SUB0H264_SPEC_REF_TRANSFORM_HPP

#include "spec_ref_tables.hpp"

#include <cstdint>
#include <algorithm>

namespace sub0h264 {
namespace spec_ref {

/** Inverse quantize a 4x4 block of coefficients.
 *
 *  ITU-T H.264 Section 8.5.12.1:
 *  For a non-I_16x16 luma block or chroma AC block:
 *    if qP >= 24:
 *      d[i][j] = (c[i][j] * LevelScale(qP%6, i, j)) << (qP/6 - 4)
 *    else:
 *      d[i][j] = (c[i][j] * LevelScale(qP%6, i, j) + (1 << (3 - qP/6))) >> (4 - qP/6)
 *
 *  @param coeffs  16 coefficients in raster order (row-major 4x4), modified in place
 *  @param qp      Quantization parameter (0..51)
 */
inline void inverseQuantize4x4(int16_t coeffs[16], int32_t qp) noexcept
{
    int32_t qpMod6 = qp % 6;
    int32_t qpDiv6 = qp / 6;

    for (uint32_t i = 0U; i < 16U; ++i) {
        if (coeffs[i] == 0) continue;

        uint8_t posClass = cPosClass4x4[i];
        int32_t levelScale = cDequantScale[qpMod6][posClass];

        // ITU-T H.264 Section 8.5.12.1
        if (qpDiv6 >= 4) {
            coeffs[i] = static_cast<int16_t>(
                static_cast<int32_t>(coeffs[i]) * levelScale << (qpDiv6 - 4));
        } else {
            int32_t shift = 4 - qpDiv6;
            int32_t offset = 1 << (shift - 1);
            coeffs[i] = static_cast<int16_t>(
                (static_cast<int32_t>(coeffs[i]) * levelScale + offset) >> shift);
        }
    }
}

/** Inverse quantize a 4x4 DC block for I_16x16 luma.
 *
 *  ITU-T H.264 Section 8.5.12.1:
 *  For the DC coefficients of I_16x16 luma, the dequant formula is:
 *    if qP >= 36:
 *      f[i][j] = (c[i][j] * LevelScale(qP%6, 0, 0)) << (qP/6 - 6)
 *    else:
 *      f[i][j] = (c[i][j] * LevelScale(qP%6, 0, 0) + (1 << (5 - qP/6))) >> (6 - qP/6)
 *
 *  Note: Position class is always 0 for DC (position (0,0)).
 *
 *  @param dc    16 DC coefficients, modified in place
 *  @param qp    Quantization parameter (0..51)
 */
inline void inverseQuantize4x4LumaDc(int16_t dc[16], int32_t qp) noexcept
{
    int32_t qpMod6 = qp % 6;
    int32_t qpDiv6 = qp / 6;
    int32_t levelScale = cDequantScale[qpMod6][0]; // Position class 0 for DC

    for (uint32_t i = 0U; i < 16U; ++i) {
        if (dc[i] == 0) continue;

        if (qpDiv6 >= 6) {
            dc[i] = static_cast<int16_t>(
                static_cast<int32_t>(dc[i]) * levelScale << (qpDiv6 - 6));
        } else {
            int32_t shift = 6 - qpDiv6;
            int32_t offset = 1 << (shift - 1);
            dc[i] = static_cast<int16_t>(
                (static_cast<int32_t>(dc[i]) * levelScale + offset) >> shift);
        }
    }
}

/** Inverse quantize chroma DC block (2x2 for 4:2:0).
 *
 *  ITU-T H.264 Section 8.5.12.1:
 *  For chroma DC:
 *    if qP >= 6:
 *      f[i][j] = (c[i][j] * LevelScale(qP%6, 0, 0)) << (qP/6 - 1)
 *    else:
 *      f[i][j] = (c[i][j] * LevelScale(qP%6, 0, 0)) >> 1
 *
 *  @param dc    4 chroma DC coefficients, modified in place
 *  @param qpC   Chroma QP (0..39, from Table 8-13 mapping)
 */
inline void inverseQuantizeChromaDc(int16_t dc[4], int32_t qpC) noexcept
{
    int32_t qpMod6 = qpC % 6;
    int32_t qpDiv6 = qpC / 6;
    int32_t levelScale = cDequantScale[qpMod6][0]; // Position class 0

    for (uint32_t i = 0U; i < 4U; ++i) {
        if (dc[i] == 0) continue;

        if (qpDiv6 >= 1) {
            dc[i] = static_cast<int16_t>(
                static_cast<int32_t>(dc[i]) * levelScale << (qpDiv6 - 1));
        } else {
            // qpDiv6 == 0
            dc[i] = static_cast<int16_t>(
                (static_cast<int32_t>(dc[i]) * levelScale) >> 1);
        }
    }
}

/** Inverse Hadamard transform for 2x2 chroma DC block.
 *
 *  ITU-T H.264 Section 8.5.11.2:
 *  Input: 4 quantized DC coefficients in scan order [0,1,2,3]
 *  Arranged as 2x2: c[0] c[1]
 *                    c[2] c[3]
 *
 *  dcY[0][0] = c[0] + c[1] + c[2] + c[3]
 *  dcY[0][1] = c[0] - c[1] + c[2] - c[3]
 *  dcY[1][0] = c[0] + c[1] - c[2] - c[3]
 *  dcY[1][1] = c[0] - c[1] - c[2] + c[3]
 *
 *  @param dc  4 DC coefficients, transformed in place
 */
inline void inverseHadamard2x2(int16_t dc[4]) noexcept
{
    int16_t a = dc[0];
    int16_t b = dc[1];
    int16_t c = dc[2];
    int16_t d = dc[3];

    dc[0] = static_cast<int16_t>(a + b + c + d);
    dc[1] = static_cast<int16_t>(a - b + c - d);
    dc[2] = static_cast<int16_t>(a + b - c - d);
    dc[3] = static_cast<int16_t>(a - b - c + d);
}

/** Inverse Hadamard transform for 4x4 luma DC block (I_16x16 mode).
 *
 *  ITU-T H.264 Section 8.5.11.1:
 *  Two-pass butterfly: horizontal then vertical on 4x4 matrix.
 *
 *  The spec defines:
 *    Horizontal: for each row i:
 *      f[i][0] = c[i][0] + c[i][1] + c[i][2] + c[i][3]
 *      f[i][1] = c[i][0] + c[i][1] - c[i][2] - c[i][3]
 *      f[i][2] = c[i][0] - c[i][1] - c[i][2] + c[i][3]
 *      f[i][3] = c[i][0] - c[i][1] + c[i][2] - c[i][3]
 *    Vertical: for each col j:
 *      dcY[j][0] = f[0][j] + f[1][j] + f[2][j] + f[3][j]
 *      dcY[j][1] = f[0][j] + f[1][j] - f[2][j] - f[3][j]
 *      dcY[j][2] = f[0][j] - f[1][j] - f[2][j] + f[3][j]
 *      dcY[j][3] = f[0][j] - f[1][j] + f[2][j] - f[3][j]
 *
 *  @param dc  16 DC coefficients in raster order (row-major 4x4), transformed in place
 */
inline void inverseHadamard4x4(int16_t dc[16]) noexcept
{
    int16_t tmp[16];

    // Horizontal transform -- ITU-T H.264 Section 8.5.11.1
    for (uint32_t i = 0U; i < 4U; ++i) {
        int32_t a = dc[i * 4 + 0];
        int32_t b = dc[i * 4 + 1];
        int32_t c = dc[i * 4 + 2];
        int32_t d = dc[i * 4 + 3];

        tmp[i * 4 + 0] = static_cast<int16_t>(a + b + c + d);
        tmp[i * 4 + 1] = static_cast<int16_t>(a + b - c - d);
        tmp[i * 4 + 2] = static_cast<int16_t>(a - b - c + d);
        tmp[i * 4 + 3] = static_cast<int16_t>(a - b + c - d);
    }

    // Vertical transform -- ITU-T H.264 Section 8.5.11.1
    for (uint32_t j = 0U; j < 4U; ++j) {
        int32_t a = tmp[0 * 4 + j];
        int32_t b = tmp[1 * 4 + j];
        int32_t c = tmp[2 * 4 + j];
        int32_t d = tmp[3 * 4 + j];

        dc[0 * 4 + j] = static_cast<int16_t>(a + b + c + d);
        dc[1 * 4 + j] = static_cast<int16_t>(a + b - c - d);
        dc[2 * 4 + j] = static_cast<int16_t>(a - b - c + d);
        dc[3 * 4 + j] = static_cast<int16_t>(a - b + c - d);
    }
}

/** Inverse Hadamard + dequantization for 4x4 luma DC (I_16x16 mode).
 *
 *  ITU-T H.264 Sections 8.5.11.1 + 8.5.12.1:
 *  Step 1: Inverse 4x4 Hadamard transform on the DC block
 *  Step 2: Inverse quantize using the I_16x16 DC formula
 *
 *  The output values go directly into each 4x4 block's DC coefficient position
 *  for subsequent IDCT processing.
 *
 *  FIX: Previous implementation used a combined formula with an erroneous
 *  factor of 16 (from the default scaling list value). The spec's DC dequant
 *  formula does NOT include scaling list multiplication for the standard case
 *  (flat_4x4_16 default). Using the two-step spec approach eliminates the bug.
 *
 *  @param input   16 DC coefficients in raster order (from CABAC + inverse scan)
 *  @param output  16 dequantized DC values, one per 4x4 block
 *  @param qp      Quantization parameter (0..51)
 */
inline void inverseHadamardDequant4x4LumaDc(const int16_t input[16],
                                              int16_t output[16],
                                              int32_t qp) noexcept
{
    // Step 1: Copy input and apply inverse Hadamard -- ITU-T H.264 Section 8.5.11.1
    for (uint32_t i = 0U; i < 16U; ++i) {
        output[i] = input[i];
    }
    inverseHadamard4x4(output);

    // Step 2: Inverse quantize using I_16x16 DC formula -- ITU-T H.264 Section 8.5.12.1
    inverseQuantize4x4LumaDc(output, qp);
}

/** Perform 4x4 inverse integer DCT and add prediction.
 *
 *  ITU-T H.264 Section 8.5.12.2:
 *  The spec defines the inverse transform as a butterfly:
 *
 *  Horizontal 1D transform for each row:
 *    e0 = d[0] + d[2]
 *    e1 = d[0] - d[2]
 *    e2 = (d[1] >> 1) - d[3]
 *    e3 = d[1] + (d[3] >> 1)
 *    f[0] = e0 + e3
 *    f[1] = e1 + e2
 *    f[2] = e1 - e2
 *    f[3] = e0 - e3
 *
 *  Vertical 1D transform for each column (same butterfly), then:
 *    output = (f + 32) >> 6
 *    pixel = Clip1Y(output + pred)  where Clip1Y clips to [0, 255]
 *
 *  @param coeffs     16 dequantized coefficients in raster order (row-major 4x4)
 *  @param pred       Pointer to top-left of prediction block
 *  @param predStride Stride of prediction buffer in bytes
 *  @param out        Pointer to top-left of output block
 *  @param outStride  Stride of output buffer in bytes
 */
inline void inverseDct4x4AddPred(const int16_t coeffs[16],
                                  const uint8_t* pred, uint32_t predStride,
                                  uint8_t* out, uint32_t outStride) noexcept
{
    int32_t tmp[16]; // Intermediate after horizontal transform

    // Horizontal 1D transform -- ITU-T H.264 Section 8.5.12.2
    for (uint32_t i = 0U; i < 4U; ++i) {
        int32_t d0 = coeffs[i * 4 + 0];
        int32_t d1 = coeffs[i * 4 + 1];
        int32_t d2 = coeffs[i * 4 + 2];
        int32_t d3 = coeffs[i * 4 + 3];

        int32_t e0 = d0 + d2;
        int32_t e1 = d0 - d2;
        int32_t e2 = (d1 >> 1) - d3;
        int32_t e3 = d1 + (d3 >> 1);

        tmp[i * 4 + 0] = e0 + e3;
        tmp[i * 4 + 1] = e1 + e2;
        tmp[i * 4 + 2] = e1 - e2;
        tmp[i * 4 + 3] = e0 - e3;
    }

    // Vertical 1D transform + round + add prediction + clip
    for (uint32_t j = 0U; j < 4U; ++j) {
        int32_t f0 = tmp[0 * 4 + j];
        int32_t f1 = tmp[1 * 4 + j];
        int32_t f2 = tmp[2 * 4 + j];
        int32_t f3 = tmp[3 * 4 + j];

        int32_t g0 = f0 + f2;
        int32_t g1 = f0 - f2;
        int32_t g2 = (f1 >> 1) - f3;
        int32_t g3 = f1 + (f3 >> 1);

        int32_t h0 = g0 + g3;
        int32_t h1 = g1 + g2;
        int32_t h2 = g1 - g2;
        int32_t h3 = g0 - g3;

        // ITU-T H.264 Section 8.5.12.2: (result + 32) >> 6, then add prediction
        for (uint32_t k = 0U; k < 4U; ++k) {
            int32_t h;
            switch (k) {
                case 0: h = h0; break;
                case 1: h = h1; break;
                case 2: h = h2; break;
                default: h = h3; break;
            }
            int32_t residual = (h + 32) >> 6;
            int32_t pixel = static_cast<int32_t>(pred[k * predStride + j]) + residual;
            out[k * outStride + j] = static_cast<uint8_t>(
                std::clamp(pixel, 0, 255));
        }
    }
}

/** Perform 4x4 inverse integer DCT without adding prediction.
 *
 *  Same as inverseDct4x4AddPred but writes raw residual values (clamped int16).
 *  Used for testing dequant+IDCT independently.
 *
 *  @param coeffs  16 dequantized coefficients in raster order, modified in place
 */
inline void inverseDct4x4(int16_t coeffs[16]) noexcept
{
    int32_t tmp[16];

    // Horizontal 1D transform
    for (uint32_t i = 0U; i < 4U; ++i) {
        int32_t d0 = coeffs[i * 4 + 0];
        int32_t d1 = coeffs[i * 4 + 1];
        int32_t d2 = coeffs[i * 4 + 2];
        int32_t d3 = coeffs[i * 4 + 3];

        int32_t e0 = d0 + d2;
        int32_t e1 = d0 - d2;
        int32_t e2 = (d1 >> 1) - d3;
        int32_t e3 = d1 + (d3 >> 1);

        tmp[i * 4 + 0] = e0 + e3;
        tmp[i * 4 + 1] = e1 + e2;
        tmp[i * 4 + 2] = e1 - e2;
        tmp[i * 4 + 3] = e0 - e3;
    }

    // Vertical 1D transform + round
    for (uint32_t j = 0U; j < 4U; ++j) {
        int32_t f0 = tmp[0 * 4 + j];
        int32_t f1 = tmp[1 * 4 + j];
        int32_t f2 = tmp[2 * 4 + j];
        int32_t f3 = tmp[3 * 4 + j];

        int32_t g0 = f0 + f2;
        int32_t g1 = f0 - f2;
        int32_t g2 = (f1 >> 1) - f3;
        int32_t g3 = f1 + (f3 >> 1);

        coeffs[0 * 4 + j] = static_cast<int16_t>((g0 + g3 + 32) >> 6);
        coeffs[1 * 4 + j] = static_cast<int16_t>((g1 + g2 + 32) >> 6);
        coeffs[2 * 4 + j] = static_cast<int16_t>((g1 - g2 + 32) >> 6);
        coeffs[3 * 4 + j] = static_cast<int16_t>((g0 - g3 + 32) >> 6);
    }
}

} // namespace spec_ref
} // namespace sub0h264

#endif // CROG_SUB0H264_SPEC_REF_TRANSFORM_HPP
