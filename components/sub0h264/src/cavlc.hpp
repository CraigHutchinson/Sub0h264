/** Sub0h264 — CAVLC entropy decoder
 *
 *  Context-Adaptive Variable-Length Coding for H.264 Baseline profile.
 *  Decodes macroblock syntax elements: mb_type, prediction modes,
 *  motion vectors, CBP, QP delta, and residual coefficients.
 *
 *  Reference: ITU-T H.264 §9.2
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef CROG_SUB0H264_CAVLC_HPP
#define CROG_SUB0H264_CAVLC_HPP

#include "bitstream.hpp"
#include "tables.hpp"
#include "sub0h264/sub0h264_types.hpp"

#include <cstdint>
#include <cstring>
#include <array>
#include <algorithm>

namespace sub0h264 {

/// Maximum coefficients in a 4x4 block.
inline constexpr uint32_t cMaxCoeff4x4 = 16U;

/// Maximum trailing ones — ITU-T H.264 §9.2.1.
inline constexpr uint32_t cMaxTrailingOnes = 3U;

/// Maximum suffix length for level decoding — ITU-T H.264 §9.2.2.
inline constexpr uint32_t cMaxSuffixLength = 6U;

// ── Coeff token decoding — ITU-T H.264 §9.2.1 ─────────────────────────

/** Result of coeff_token VLC decoding. */
struct CoeffToken
{
    uint8_t totalCoeff;    ///< Number of non-zero coefficients [0-16]
    uint8_t trailingOnes;  ///< Number of trailing ±1 coefficients [0-3]
};

/** Decode coeff_token from bitstream using context nC.
 *
 *  nC (number of coefficients context) is derived from neighboring blocks:
 *  nC = (leftNnz + topNnz + 1) >> 1
 *
 *  @param br   Bitstream reader
 *  @param nC   Context value [0-16, or -1 for chroma DC, -2 for luma DC of I16x16]
 *  @return Decoded coeff_token
 *
 *  Reference: ITU-T H.264 §9.2.1, Tables 9-5(a-d)
 */
inline CoeffToken decodeCoeffToken(BitReader& br, int32_t nC) noexcept
{
    CoeffToken ct = { 0U, 0U };

    if (nC < 0)
    {
        // Chroma DC or special case — use simplified table
        // For chroma DC (nC == -1): Table 9-5(d)
        uint32_t code = br.peekBits(8U);
        if (code >= 128U)      { ct = {0U, 0U}; br.skipBits(1U); }
        else if (code >= 64U)  { ct = {1U, 1U}; br.skipBits(2U); }
        else if (code >= 32U)  { ct = {2U, 1U}; br.skipBits(3U); }
        else if (code >= 24U)  { ct = {3U, 1U}; br.skipBits(4U); }
        else if (code >= 20U)  { ct = {4U, 1U}; br.skipBits(5U); }
        else if (code >= 16U)  { ct = {1U, 0U}; br.skipBits(5U); }
        else if (code >= 12U)  { ct = {2U, 2U}; br.skipBits(4U); }
        else if (code >= 10U)  { ct = {3U, 2U}; br.skipBits(5U); }
        else if (code >= 8U)   { ct = {4U, 2U}; br.skipBits(5U); }
        else if (code >= 7U)   { ct = {3U, 3U}; br.skipBits(6U); }
        else if (code >= 6U)   { ct = {4U, 3U}; br.skipBits(6U); }
        else if (code >= 4U)   { ct = {2U, 0U}; br.skipBits(7U); }
        else if (code >= 2U)   { ct = {3U, 0U}; br.skipBits(8U); }
        else                   { ct = {4U, 0U}; br.skipBits(8U); }
        return ct;
    }

    if (nC < 2)
    {
        // Table 9-5(a): 0 <= nC < 2
        uint32_t code = br.peekBits(16U);
        if      (code >= 0x8000U) { ct = {0U, 0U}; br.skipBits(1U); }
        else if (code >= 0x2000U) { ct = {1U, 1U}; br.skipBits(2U); }   // 01
        else if (code >= 0x1000U) { ct = {2U, 2U}; br.skipBits(4U); }   // 0001xx → need more detail
        else
        {
            // Fall through to bit-by-bit decode for longer codes
            // Count leading zeros for prefix
            uint32_t lz = 0U;
            while (lz < 16U && br.peekBits(1U) == 0U)
            {
                br.skipBits(1U);
                ++lz;
            }
            br.skipBits(1U); // skip the '1' bit

            // For nC<2, total_coeff and trailing_ones are encoded with varying-length codes
            // This is a simplified decoder — for production, a full table would be used
            uint32_t suffix = (lz > 0U) ? br.readBits(lz) : 0U;
            uint32_t value = (1U << lz) - 1U + suffix;

            // Map to total_coeff, trailing_ones (simplified — covers common cases)
            if (value == 0U)      ct = {1U, 1U};
            else if (value <= 2U) { ct.totalCoeff = static_cast<uint8_t>(value); ct.trailingOnes = static_cast<uint8_t>(std::min(value, 3U)); }
            else                  { ct.totalCoeff = static_cast<uint8_t>(std::min(value, 16U)); ct.trailingOnes = static_cast<uint8_t>(std::min(value & 3U, 3U)); }
        }
        return ct;
    }

    if (nC < 4)
    {
        // Table 9-5(b): 2 <= nC < 4
        uint32_t code = br.peekBits(14U);
        if      (code >= 0x2000U) { ct = {0U, 0U}; br.skipBits(2U); }   // 11
        else if (code >= 0x1800U) { ct = {1U, 1U}; br.skipBits(2U); }   // 10
        else
        {
            // Longer codes — simplified
            uint32_t lz = 0U;
            while (lz < 14U && br.peekBits(1U) == 0U)
            {
                br.skipBits(1U);
                ++lz;
            }
            br.skipBits(1U);
            uint32_t suffix = (lz > 0U) ? br.readBits(std::min(lz, 4U)) : 0U;
            uint32_t value = (1U << lz) - 1U + suffix;
            ct.totalCoeff = static_cast<uint8_t>(std::min(value + 1U, 16U));
            ct.trailingOnes = static_cast<uint8_t>(std::min(value & 3U, 3U));
        }
        return ct;
    }

    if (nC < 8)
    {
        // Table 9-5(c): 4 <= nC < 8
        // Uses 6-bit fixed-length codes
        uint32_t code = br.readBits(6U);
        ct.trailingOnes = static_cast<uint8_t>(code & 3U);
        ct.totalCoeff = static_cast<uint8_t>((code >> 2U) + ct.trailingOnes);
        if (ct.totalCoeff > 16U) ct.totalCoeff = 16U;
        return ct;
    }

    // nC >= 8: fixed-length 6-bit code — Table 9-5(e)
    uint32_t code = br.readBits(6U);
    ct.trailingOnes = static_cast<uint8_t>(code & 3U);
    ct.totalCoeff = static_cast<uint8_t>((code >> 2U) + ct.trailingOnes);
    if (ct.totalCoeff > 16U) ct.totalCoeff = 16U;
    return ct;
}

// ── Level decoding — ITU-T H.264 §9.2.2 ────────────────────────────────

/** Decode one coefficient level value.
 *  @param br         Bitstream reader
 *  @param suffixLen  Current suffix length [0-6], updated after decode
 *  @return Decoded signed level value
 */
inline int32_t decodeLevel(BitReader& br, uint32_t& suffixLen) noexcept
{
    // Count leading zeros (level prefix)
    uint32_t prefix = 0U;
    while (prefix < 32U && br.readBit() == 0U)
        ++prefix;

    int32_t levelCode = static_cast<int32_t>(std::min(prefix, 15U));

    uint32_t suffixSize = suffixLen;
    if (prefix == 14U && suffixLen == 0U)
        suffixSize = 4U;
    else if (prefix >= 15U)
        suffixSize = prefix - 3U;

    if (suffixSize > 0U && br.hasBits(suffixSize))
        levelCode += static_cast<int32_t>(br.readBits(suffixSize)) ;

    if (prefix >= 15U && suffixLen == 0U)
        levelCode += 15;
    if (prefix >= 16U)
        levelCode += (1 << (prefix - 3U)) - 4096;

    // Convert to signed
    int32_t absLevel = (levelCode + 2) >> 1;
    int32_t sign = (levelCode & 1) ? -1 : 1;
    int32_t level = absLevel * sign;

    // Update suffix length — ITU-T H.264 §9.2.2
    uint32_t absVal = static_cast<uint32_t>(absLevel);
    if (suffixLen < cMaxSuffixLength && absVal > cLevelSuffixThreshold[suffixLen])
        ++suffixLen;

    return level;
}

// ── Total zeros decoding — ITU-T H.264 §9.2.3 ──────────────────────────

/** Decode total_zeros for 4x4 block.
 *  @param br          Bitstream reader
 *  @param totalCoeff  Number of non-zero coefficients [1-15]
 *  @return Total number of zero coefficients before the last non-zero
 */
inline uint32_t decodeTotalZeros(BitReader& br, uint32_t totalCoeff) noexcept
{
    // ITU-T H.264 Table 9-7: VLC for total_zeros
    // Decode using bit-by-bit leading-zero prefix approach
    if (totalCoeff >= 16U)
        return 0U;

    uint32_t maxZeros = 16U - totalCoeff;

    // Simplified: use prefix + suffix decoding
    uint32_t prefix = 0U;
    while (prefix < 9U && br.readBit() == 0U)
        ++prefix;

    // The mapping from (totalCoeff, prefix) to total_zeros varies per table
    // For correctness, this would need full VLC tables. Simplified here
    // to use the prefix as a reasonable approximation for initial testing.
    uint32_t totalZeros = std::min(prefix, maxZeros);
    return totalZeros;
}

// ── Run before decoding — ITU-T H.264 §9.2.3 ───────────────────────────

/** Decode run_before (zeros before a coefficient).
 *  @param br         Bitstream reader
 *  @param zerosLeft  Remaining zeros to distribute
 *  @return Run length before this coefficient
 */
inline uint32_t decodeRunBefore(BitReader& br, uint32_t zerosLeft) noexcept
{
    if (zerosLeft == 0U)
        return 0U;

    // ITU-T H.264 Table 9-10
    if (zerosLeft == 1U)
        return br.readBit(); // 0→0, 1→1

    if (zerosLeft == 2U)
    {
        uint32_t code = br.peekBits(2U);
        if (code >= 2U) { br.skipBits(1U); return 0U; }
        if (code == 1U) { br.skipBits(2U); return 1U; }
        br.skipBits(2U); return 2U;
    }

    if (zerosLeft <= 6U)
    {
        uint32_t code = br.peekBits(3U);
        if (code >= 4U) { br.skipBits(2U); return static_cast<uint32_t>(code >= 6U ? 0U : 1U); }
        if (code == 3U) { br.skipBits(2U); return 2U; }
        if (code == 2U) { br.skipBits(3U); return 3U; }
        if (code == 1U) { br.skipBits(3U); return std::min(zerosLeft - 1U, 4U); }
        // code == 0
        br.skipBits(3U);
        return std::min(zerosLeft, 5U);
    }

    // zerosLeft > 6: prefix coding
    uint32_t prefix = 0U;
    while (prefix < 11U && br.readBit() == 0U)
        ++prefix;

    if (prefix <= 6U)
        return prefix;
    return std::min(prefix, zerosLeft);
}

// ── 4x4 residual block decoder — ITU-T H.264 §9.2 ─────────────────────

/// Decoded residual coefficients for one 4x4 block.
struct ResidualBlock4x4
{
    int16_t coeffs[16]{};    ///< Coefficients in raster order
    uint8_t totalCoeff = 0U; ///< Non-zero coefficient count (for neighbor context)
};

/** Decode a 4x4 residual block using CAVLC.
 *
 *  @param br         Bitstream reader
 *  @param nC         Context from neighboring blocks
 *  @param maxCoeff   Maximum coefficients (16 for luma, 15 for AC-only)
 *  @param startIdx   Starting scan index (0 for DC+AC, 1 for AC-only)
 *  @param[out] block Decoded coefficients in raster order
 *  @return Result::Ok on success
 */
inline Result decodeResidualBlock4x4(BitReader& br, int32_t nC,
                                      uint32_t maxCoeff, uint32_t startIdx,
                                      ResidualBlock4x4& block) noexcept
{
    std::memset(&block, 0, sizeof(block));

    // 1. Decode coeff_token
    CoeffToken ct = decodeCoeffToken(br, nC);
    block.totalCoeff = ct.totalCoeff;

    if (ct.totalCoeff == 0U)
        return Result::Ok;

    // 2. Decode trailing ones (±1 signs)
    std::array<int16_t, 16> levels{};
    uint32_t levelIdx = 0U;

    for (uint32_t i = 0U; i < ct.trailingOnes && i < ct.totalCoeff; ++i)
    {
        uint32_t sign = br.readBit();
        levels[levelIdx++] = sign ? -1 : 1;
    }

    // 3. Decode remaining levels
    uint32_t suffixLen = 0U;
    if (ct.totalCoeff > 10U && ct.trailingOnes < cMaxTrailingOnes)
        suffixLen = 1U;

    for (uint32_t i = ct.trailingOnes; i < ct.totalCoeff; ++i)
    {
        int32_t level = decodeLevel(br, suffixLen);

        // First non-trailing level has +1/-1 offset if trailing_ones < 3
        if (i == ct.trailingOnes && ct.trailingOnes < cMaxTrailingOnes)
        {
            level += (level > 0) ? 1 : -1;
        }

        levels[levelIdx++] = static_cast<int16_t>(level);
    }

    // 4. Decode total_zeros
    uint32_t totalZeros = 0U;
    if (ct.totalCoeff < maxCoeff)
        totalZeros = decodeTotalZeros(br, ct.totalCoeff);

    // 5. Decode run_before and map to scan positions
    uint32_t zerosLeft = totalZeros;
    uint32_t coeffIdx = ct.totalCoeff + totalZeros - 1U + startIdx;

    for (uint32_t i = 0U; i < ct.totalCoeff; ++i)
    {
        uint32_t run = 0U;
        if (zerosLeft > 0U && i < ct.totalCoeff - 1U)
        {
            run = decodeRunBefore(br, zerosLeft);
            zerosLeft -= run;
        }
        else if (i == ct.totalCoeff - 1U)
        {
            run = zerosLeft;
        }

        // Map scan position to raster position via zigzag
        if (coeffIdx < maxCoeff)
        {
            uint32_t rasterPos = cZigzag4x4[coeffIdx];
            block.coeffs[rasterPos] = levels[i];
        }

        if (coeffIdx >= run)
            coeffIdx -= (run + 1U);
    }

    return Result::Ok;
}

// ── Macroblock types — ITU-T H.264 Tables 7-11, 7-13 ───────────────────

/// I-slice macroblock types — ITU-T H.264 Table 7-11.
enum class IMbType : uint8_t
{
    I_4x4    = 0U,
    I_16x16_0_0_0 = 1U,  ///< I_16x16 with pred_mode=0, cbp_luma=0, cbp_chroma=0
    I_16x16_1_0_0 = 2U,
    I_16x16_2_0_0 = 3U,
    I_16x16_3_0_0 = 4U,
    I_16x16_0_1_0 = 5U,
    I_16x16_1_1_0 = 6U,
    I_16x16_2_1_0 = 7U,
    I_16x16_3_1_0 = 8U,
    I_16x16_0_2_0 = 9U,
    I_16x16_1_2_0 = 10U,
    I_16x16_2_2_0 = 11U,
    I_16x16_3_2_0 = 12U,
    I_16x16_0_0_15 = 13U,
    I_16x16_1_0_15 = 14U,
    I_16x16_2_0_15 = 15U,
    I_16x16_3_0_15 = 16U,
    I_16x16_0_1_15 = 17U,
    I_16x16_1_1_15 = 18U,
    I_16x16_2_1_15 = 19U,
    I_16x16_3_1_15 = 20U,
    I_16x16_0_2_15 = 21U,
    I_16x16_1_2_15 = 22U,
    I_16x16_2_2_15 = 23U,
    I_16x16_3_2_15 = 24U,
    I_PCM         = 25U,
};

/** Extract I_16x16 macroblock properties from mb_type.
 *  For I_16x16 types (1-24), the mb_type encodes pred_mode, cbp_luma, and cbp_chroma.
 *  Reference: ITU-T H.264 Table 7-11.
 */
inline bool isI16x16(uint8_t mbType) noexcept { return mbType >= 1U && mbType <= 24U; }
inline uint8_t i16x16PredMode(uint8_t mbType) noexcept { return (mbType - 1U) % 4U; }
inline uint8_t i16x16CbpLuma(uint8_t mbType) noexcept { return ((mbType - 1U) / 4U < 3U) ? 0U : 15U; }
inline uint8_t i16x16CbpChroma(uint8_t mbType) noexcept { return ((mbType - 1U) / 4U) % 3U; }

/// P-slice macroblock types — ITU-T H.264 Table 7-13.
enum class PMbType : uint8_t
{
    P_L0_16x16  = 0U,
    P_L0_L0_16x8 = 1U,
    P_L0_L0_8x16 = 2U,
    P_8x8       = 3U,
    P_8x8ref0   = 4U,
};

// ── Macroblock data — decoded syntax elements for one MB ────────────────

/// Decoded macroblock data from CAVLC parsing.
struct MacroblockData
{
    uint8_t mbType = 0U;
    bool isIntra = false;           ///< True for I-MB types
    bool isSkipped = false;         ///< True for P_Skip
    bool isI16x16 = false;          ///< True for I_16x16 subtypes

    // Intra prediction
    uint8_t intraPredMode16x16 = 0U;
    uint8_t intraPredMode4x4[16]{};
    uint8_t intraChromaPredMode = 0U;

    // Inter prediction
    int16_t mvdL0[4][2]{};         ///< Motion vector differences [partition][x,y]
    uint8_t refIdxL0[4]{};         ///< Reference frame indices
    uint8_t numPartitions = 0U;

    // Coded block pattern
    uint8_t cbpLuma = 0U;          ///< Bits [3:0] for 4 8x8 luma blocks
    uint8_t cbpChroma = 0U;        ///< 0=none, 1=DC only, 2=DC+AC

    // QP
    int8_t qpDelta = 0;
    int32_t qp = 0;                ///< Effective QP for this MB

    // Residual coefficients
    ResidualBlock4x4 lumaDc;                ///< I_16x16 DC block
    ResidualBlock4x4 lumaBlocks[16]{};      ///< 16 luma 4x4 blocks
    ResidualBlock4x4 chromaDcCb;            ///< Chroma Cb DC
    ResidualBlock4x4 chromaDcCr;            ///< Chroma Cr DC
    ResidualBlock4x4 chromaBlocksCb[4]{};   ///< 4 chroma Cb AC blocks
    ResidualBlock4x4 chromaBlocksCr[4]{};   ///< 4 chroma Cr AC blocks

    // Non-zero coefficient counts (for neighbor context)
    uint8_t nnz[16]{};             ///< Non-zero count per 4x4 luma block
    uint8_t nnzCb[4]{};            ///< Non-zero count per chroma Cb block
    uint8_t nnzCr[4]{};            ///< Non-zero count per chroma Cr block
};

} // namespace sub0h264

#endif // CROG_SUB0H264_CAVLC_HPP
