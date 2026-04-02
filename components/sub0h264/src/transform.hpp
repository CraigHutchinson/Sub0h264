/** Sub0h264 — Inverse transform + quantization + reconstruction
 *
 *  Implements the H.264 4x4 inverse integer DCT, Hadamard transforms,
 *  inverse quantization, and pixel reconstruction.
 *
 *  Reference: ITU-T H.264 §8.5.12 (inverse transform)
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef CROG_SUB0H264_TRANSFORM_HPP
#define CROG_SUB0H264_TRANSFORM_HPP

#include <cstdint>
#include <algorithm>
#include <array>

namespace sub0h264 {

/// Clip value to [0, 255] for 8-bit pixel output.
inline constexpr int32_t clipU8(int32_t val) noexcept
{
    return val < 0 ? 0 : (val > 255 ? 255 : val);
}

/// Clamp QP-derived index to [0, 51]. Avoids std::min/max type deduction issues.
inline constexpr int32_t clampQpIdx(int32_t val) noexcept
{
    return val < 0 ? 0 : (val > 51 ? 51 : val);
}

// ── Default dequantization scaling — ITU-T H.264 §8.5.12.1 ─────────────

/// Dequantization scale factors for 4x4 blocks, indexed by QP%6 and position class.
/// v[qp%6][position_class] where:
///   class 0 = both-even (e.g., (0,0),(0,2),(2,0),(2,2))
///   class 1 = both-odd  (e.g., (1,1),(1,3),(3,1),(3,3))
///   class 2 = mixed     (all other positions)
/// Reference: ITU-T H.264 Table 8-15, LevelScale4x4[k][i][j].
/// Column order: {class0, class1, class2} = {both-even, both-odd, mixed}.
inline constexpr std::array<std::array<int32_t, 3>, 6> cDequantScale = {{
    {{ 10, 16, 13 }},
    {{ 11, 18, 14 }},
    {{ 13, 20, 16 }},
    {{ 14, 23, 18 }},
    {{ 16, 25, 20 }},
    {{ 18, 29, 23 }},
}};

/// Position class for each raster position in a 4x4 block — §8.5.12.1.
/// Derived from (row, col) parity:
///   both-even (0,0),(0,2),(2,0),(2,2) → class 0
///   both-odd  (1,1),(1,3),(3,1),(3,3) → class 1
///   mixed                             → class 2
/// Determines which normAdjust column is used in dequantization.
inline constexpr std::array<uint8_t, 16> cDequantPosClass = []() constexpr
{
    std::array<uint8_t, 16> t{};
    for (uint32_t i = 0U; i < 16U; ++i)
    {
        uint32_t col = i % 4U;
        uint32_t row = i / 4U;
        if (col % 2U == 0U && row % 2U == 0U)
            t[i] = 0U;
        else if (col % 2U == 1U && row % 2U == 1U)
            t[i] = 1U;
        else
            t[i] = 2U;
    }
    return t;
}();

// Compile-time spot-checks — ITU-T H.264 §8.5.12.1 Table 8-15.
static_assert(cDequantPosClass[0]  == 0U, "pos(0,0) → class 0");
static_assert(cDequantPosClass[5]  == 1U, "pos(1,1) → class 1");
static_assert(cDequantPosClass[1]  == 2U, "pos(0,1) → class 2");
static_assert(cDequantPosClass[15] == 1U, "pos(3,3) → class 1");
// Scale value checks: both-odd (class 1) must be larger than both-even (class 0)
// per Table 8-15, e.g., qp%6=0 → class0=10, class1=16, class2=13.
static_assert(cDequantScale[0][0] == 10, "qp%6=0, class0 = 10 per Table 8-15");
static_assert(cDequantScale[0][1] == 16, "qp%6=0, class1 = 16 per Table 8-15");
static_assert(cDequantScale[0][2] == 13, "qp%6=0, class2 = 13 per Table 8-15");
static_assert(cDequantScale[3][1] == 23, "qp%6=3, class1 = 23 per Table 8-15");

// ── 4x4 Inverse Integer DCT — ITU-T H.264 §8.5.12 ─────────────────────

/** Apply inverse 4x4 integer DCT and add to prediction, storing result.
 *
 *  Implements the butterfly in ITU-T H.264 §8.5.12.2.
 *  The >> 6 normalization is absorbed from the dequant scaling.
 *
 *  @param coeffs     Input: 16 dequantized coefficients in raster order
 *  @param pred       Input: 4x4 prediction block (stride = predStride)
 *  @param out        Output: 4x4 reconstructed block (stride = outStride)
 *  @param predStride Stride of prediction buffer
 *  @param outStride  Stride of output buffer
 */
inline void inverseDct4x4AddPred(const int16_t* coeffs,
                                  const uint8_t* pred, uint32_t predStride,
                                  uint8_t* out, uint32_t outStride) noexcept
{
    int32_t tmp[16];

    // Horizontal transform (for each row)
    for (uint32_t i = 0U; i < 4U; ++i)
    {
        int32_t q0 = coeffs[i * 4 + 0];
        int32_t q1 = coeffs[i * 4 + 1];
        int32_t q2 = coeffs[i * 4 + 2];
        int32_t q3 = coeffs[i * 4 + 3];

        int32_t x0 = q0 + q2;
        int32_t x1 = q0 - q2;
        int32_t x2 = (q1 >> 1) - q3;
        int32_t x3 = q1 + (q3 >> 1);

        tmp[i * 4 + 0] = x0 + x3;
        tmp[i * 4 + 1] = x1 + x2;
        tmp[i * 4 + 2] = x1 - x2;
        tmp[i * 4 + 3] = x0 - x3;
    }

    // Vertical transform (for each column) + prediction + clip
    for (uint32_t j = 0U; j < 4U; ++j)
    {
        int32_t t0 = tmp[0 * 4 + j];
        int32_t t1 = tmp[1 * 4 + j];
        int32_t t2 = tmp[2 * 4 + j];
        int32_t t3 = tmp[3 * 4 + j];

        int32_t x0 = t0 + t2;
        int32_t x1 = t0 - t2;
        int32_t x2 = (t1 >> 1) - t3;
        int32_t x3 = t1 + (t3 >> 1);

        // Scale down by 64 (6-bit shift with rounding) and add prediction
        int32_t r0 = (x0 + x3 + 32) >> 6;
        int32_t r1 = (x1 + x2 + 32) >> 6;
        int32_t r2 = (x1 - x2 + 32) >> 6;
        int32_t r3 = (x0 - x3 + 32) >> 6;

        out[0 * outStride + j] = static_cast<uint8_t>(clipU8(pred[0 * predStride + j] + r0));
        out[1 * outStride + j] = static_cast<uint8_t>(clipU8(pred[1 * predStride + j] + r1));
        out[2 * outStride + j] = static_cast<uint8_t>(clipU8(pred[2 * predStride + j] + r2));
        out[3 * outStride + j] = static_cast<uint8_t>(clipU8(pred[3 * predStride + j] + r3));
    }
}

/** DC-only fast path: add constant DC value to prediction.
 *  Used when only the DC coefficient is non-zero.
 */
inline void inverseDcOnly4x4AddPred(int16_t dcCoeff,
                                     const uint8_t* pred, uint32_t predStride,
                                     uint8_t* out, uint32_t outStride) noexcept
{
    int32_t dcVal = (dcCoeff + 32) >> 6;

    for (uint32_t row = 0U; row < 4U; ++row)
    {
        for (uint32_t col = 0U; col < 4U; ++col)
        {
            out[row * outStride + col] = static_cast<uint8_t>(
                clipU8(pred[row * predStride + col] + dcVal));
        }
    }
}

// ── 4x4 Hadamard Inverse Transform (Luma DC) — ITU-T H.264 §8.5.12.2 ──

/** Inverse 4x4 Hadamard transform for I_16x16 luma DC coefficients.
 *
 *  Transforms 16 DC values from the 4x4 DC block back to
 *  individual DC coefficients for each 4x4 sub-block.
 *
 *  @param[in,out] dc  16 DC coefficients, transformed in-place
 */
inline void inverseHadamard4x4(int16_t* dc) noexcept
{
    int32_t tmp[16];

    // Horizontal transform
    for (uint32_t i = 0U; i < 4U; ++i)
    {
        int32_t s0 = dc[i * 4 + 0];
        int32_t s1 = dc[i * 4 + 1];
        int32_t s2 = dc[i * 4 + 2];
        int32_t s3 = dc[i * 4 + 3];

        int32_t x0 = s0 + s3;
        int32_t x1 = s1 + s2;
        int32_t x2 = s1 - s2;
        int32_t x3 = s0 - s3;

        tmp[i * 4 + 0] = x0 + x1;
        tmp[i * 4 + 1] = x2 + x3;
        tmp[i * 4 + 2] = x0 - x1;
        tmp[i * 4 + 3] = x3 - x2;
    }

    // Vertical transform
    for (uint32_t j = 0U; j < 4U; ++j)
    {
        int32_t t0 = tmp[0 * 4 + j];
        int32_t t1 = tmp[1 * 4 + j];
        int32_t t2 = tmp[2 * 4 + j];
        int32_t t3 = tmp[3 * 4 + j];

        int32_t x0 = t0 + t3;
        int32_t x1 = t1 + t2;
        int32_t x2 = t1 - t2;
        int32_t x3 = t0 - t3;

        dc[0 * 4 + j] = static_cast<int16_t>(x0 + x1);
        dc[1 * 4 + j] = static_cast<int16_t>(x2 + x3);
        dc[2 * 4 + j] = static_cast<int16_t>(x0 - x1);
        dc[3 * 4 + j] = static_cast<int16_t>(x3 - x2);
    }
}

// ── 2x2 Hadamard Inverse Transform (Chroma DC) — ITU-T H.264 §8.5.12.2 ─

/** Inverse 2x2 Hadamard transform for chroma DC coefficients.
 *
 *  @param[in,out] dc  4 DC coefficients, transformed in-place
 */
inline void inverseHadamard2x2(int16_t* dc) noexcept
{
    // ITU-T H.264 §8.5.12.2: 2x2 Hadamard for chroma DC (4:2:0).
    // Reference: libavc ih264d_cavlc_parse_chroma_dc, lines 1385-1400.
    // The butterfly matches libavc's column-first ordering:
    //   z0=dc[0]+dc[2], z1=dc[0]-dc[2], z2=dc[1]-dc[3], z3=dc[1]+dc[3]
    //   out[0]=z0+z3, out[1]=z0-z3, out[2]=z1+z2, out[3]=z1-z2
    int32_t z0 = dc[0] + dc[2];
    int32_t z1 = dc[0] - dc[2];
    int32_t z2 = dc[1] - dc[3];
    int32_t z3 = dc[1] + dc[3];

    dc[0] = static_cast<int16_t>(z0 + z3);
    dc[1] = static_cast<int16_t>(z0 - z3);
    dc[2] = static_cast<int16_t>(z1 + z2);
    dc[3] = static_cast<int16_t>(z1 - z2);
}

// ── Inverse Quantization — ITU-T H.264 §8.5.12.1 ───────────────────────

/** Inverse quantize a 4x4 block of coefficients.
 *
 *  @param[in,out] coeffs  16 coefficients, dequantized in-place
 *  @param qp              Quantization parameter [0-51]
 *  @param isDc            True if this is a DC-only block (different scaling)
 */
inline void inverseQuantize4x4(int16_t* coeffs, int32_t qp, bool isDc = false) noexcept
{
    // Ensure QP is in valid range [0, 51] — protects against bitstream errors.
    qp = ((qp % 52) + 52) % 52;
    int32_t qpDiv6 = qp / 6;
    int32_t qpMod6 = qp % 6;

    for (uint32_t i = 0U; i < 16U; ++i)
    {
        if (coeffs[i] == 0)
            continue;

        int32_t posClass = cDequantPosClass[i];
        int32_t scale = cDequantScale[qpMod6][posClass];
        int32_t val = static_cast<int32_t>(coeffs[i]) * scale;

        if (isDc)
        {
            // I_16x16 Hadamard DC: ITU-T H.264 §8.5.12.1.
            // Hadamard includes its own normalization, so full << qpDiv6.
            val <<= qpDiv6;
        }
        else
        {
            // 4x4 residual blocks: ITU-T H.264 §8.5.12.1.
            // The dequant formula is: d = c * LevelScale << (qP/6).
            // The IDCT then applies >> 6 normalization at the output.
            // Combined: pixel = pred + (IDCT(c * scale << qpDiv6) + 32) >> 6.
            //
            // Note: the spec states << (qP/6 - 2) which assumes a DIFFERENT
            // IDCT normalization (>> 4 built into the scaling factors).
            // Our butterfly IDCT uses >> 6, matching libavc's implementation.
            // Reference: libavc INV_QUANT macro (ih264_trans_macros.h):
            //   val = coeff * scale * 16 << qpDiv6 >> 4 = coeff * scale << qpDiv6
            val <<= qpDiv6;
        }

        coeffs[i] = static_cast<int16_t>(val);
    }
}

} // namespace sub0h264

#endif // CROG_SUB0H264_TRANSFORM_HPP
