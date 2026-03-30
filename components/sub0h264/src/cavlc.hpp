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
#include "cavlc_tables.hpp"
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

/** Match a VLC code against the bitstream.
 *  Tries all (trailingOnes, totalCoeff) combinations for a given nC range.
 *  Returns the match with the shortest code that matches the peeked bits.
 */
inline CoeffToken matchCoeffTokenTable(BitReader& br, uint32_t tableIdx) noexcept
{
    // Peek enough bits for the longest possible code (16 bits)
    uint32_t peekBuf = br.peekBits(16U);

    // Search all (trailingOnes, totalCoeff) combinations, preferring shortest match
    CoeffToken best = { 0U, 0U };
    uint8_t bestSize = 255U;

    for (uint32_t to = 0U; to < 4U; ++to)
    {
        for (uint32_t tc = 0U; tc <= 16U; ++tc)
        {
            // Skip invalid combinations: trailing ones can't exceed total coefficients
            if (to > tc)
                continue;

            uint8_t codeSize = cCoeffTokenSize[tableIdx][to][tc];
            if (codeSize == 0U || codeSize > 16U)
                continue;

            uint16_t codeVal = cCoeffTokenCode[tableIdx][to][tc];

            // Extract top codeSize bits from peek buffer and compare
            uint32_t mask = (1U << codeSize) - 1U;
            uint32_t peeked = (peekBuf >> (16U - codeSize)) & mask;

            if (peeked == codeVal && codeSize < bestSize)
            {
                best.totalCoeff = static_cast<uint8_t>(tc);
                best.trailingOnes = static_cast<uint8_t>(to);
                bestSize = codeSize;
            }
        }
    }

    br.skipBits(bestSize);
    return best;
}

/** Decode coeff_token from bitstream using context nC.
 *
 *  Uses full ITU-T H.264 Table 9-5 VLC lookup tables for spec-compliant decoding.
 *  nC (number of coefficients context) is derived from neighboring blocks.
 *
 *  @param br   Bitstream reader
 *  @param nC   Context value [0-16, or -1 for chroma DC]
 *  @return Decoded coeff_token
 *
 *  Reference: ITU-T H.264 §9.2.1, Tables 9-5(a-e)
 */
inline CoeffToken decodeCoeffToken(BitReader& br, int32_t nC) noexcept
{
    if (nC < 0)
    {
        // Chroma DC: Table 9-5(d) — max 4 coefficients
        uint32_t peekBuf = br.peekBits(8U);
        CoeffToken best = { 0U, 0U };
        uint8_t bestSize = 255U;

        for (uint32_t to = 0U; to < 4U; ++to)
        {
            for (uint32_t tc = 0U; tc <= 4U; ++tc)
            {
                if (to > tc)
                    continue;
                uint8_t codeSize = cCoeffTokenSizeChroma[to][tc];
                if (codeSize == 0U)
                    continue;
                uint8_t codeVal = cCoeffTokenCodeChroma[to][tc];
                uint32_t peeked = (peekBuf >> (8U - codeSize)) & ((1U << codeSize) - 1U);
                if (peeked == codeVal && codeSize < bestSize)
                {
                    best.totalCoeff = static_cast<uint8_t>(tc);
                    best.trailingOnes = static_cast<uint8_t>(to);
                    bestSize = codeSize;
                }
            }
        }
        br.skipBits(bestSize);
        return best;
    }

    if (nC >= 8)
    {
        // Table 9-5(e): fixed 6-bit code for nC >= 8
        uint32_t code = br.readBits(6U);
        CoeffToken ct;
        ct.trailingOnes = static_cast<uint8_t>(code & 3U);
        ct.totalCoeff = static_cast<uint8_t>((code >> 2U));
        if (ct.trailingOnes > ct.totalCoeff)
            ct.trailingOnes = ct.totalCoeff;
        return ct;
    }

    // Tables 9-5(a), (b), (c): select table by nC range
    uint32_t tableIdx;
    if (nC < 2)       tableIdx = 0U;
    else if (nC < 4)  tableIdx = 1U;
    else              tableIdx = 2U;  // 4 <= nC < 8

    return matchCoeffTokenTable(br, tableIdx);
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

    int32_t levelCode = static_cast<int32_t>(prefix < 15U ? prefix : 15U);

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

/** Decode total_zeros for 4x4 block using spec VLC tables.
 *  @param br          Bitstream reader
 *  @param totalCoeff  Number of non-zero coefficients [1-15]
 *  @return Total number of zero coefficients before the last non-zero
 *
 *  Reference: ITU-T H.264 §9.2.3, Tables 9-7/9-8
 */
inline uint32_t decodeTotalZeros(BitReader& br, uint32_t totalCoeff) noexcept
{
    if (totalCoeff == 0U || totalCoeff >= 16U)
        return 0U;

    uint32_t maxZeros = 16U - totalCoeff;
    uint32_t tableOffset = cTotalZerosIndex[totalCoeff - 1U];
    uint32_t tableLen = (totalCoeff < 15U)
        ? (cTotalZerosIndex[totalCoeff] - tableOffset)
        : (135U - tableOffset);

    uint32_t peekBuf = br.peekBits(9U); // Max total_zeros VLC is 9 bits

    // Search for matching VLC code
    for (uint32_t tzVal = 0U; tzVal < tableLen; ++tzVal)
    {
        uint8_t codeSize = cTotalZerosSize[tableOffset + tzVal];
        uint8_t codeVal  = cTotalZerosCode[tableOffset + tzVal];

        if (codeSize == 0U || codeSize > 9U)
            continue;

        uint32_t peeked = (peekBuf >> (9U - codeSize)) & ((1U << codeSize) - 1U);
        if (peeked == codeVal)
        {
            br.skipBits(codeSize);
            return std::min(tzVal, maxZeros);
        }
    }

    // Fallback: consume 1 bit, return 0
    br.skipBits(1U);
    return 0U;
}

// ── Run before decoding — ITU-T H.264 §9.2.3 ───────────────────────────

/** Decode run_before (zeros before a coefficient) using spec VLC tables.
 *  @param br         Bitstream reader
 *  @param zerosLeft  Remaining zeros to distribute
 *  @return Run length before this coefficient
 *
 *  Reference: ITU-T H.264 §9.2.3, Table 9-10
 */
inline uint32_t decodeRunBefore(BitReader& br, uint32_t zerosLeft) noexcept
{
    if (zerosLeft == 0U)
        return 0U;

    if (zerosLeft <= 6U)
    {
        uint32_t tableOffset = cRunBeforeIndex[zerosLeft - 1U];
        uint32_t tableLen = (zerosLeft < 6U)
            ? (cRunBeforeIndex[zerosLeft] - tableOffset)
            : (27U - tableOffset);

        uint32_t peekBuf = br.peekBits(3U);

        for (uint32_t runVal = 0U; runVal < tableLen; ++runVal)
        {
            uint8_t codeSize = cRunBeforeSize[tableOffset + runVal];
            uint8_t codeVal  = cRunBeforeCode[tableOffset + runVal];

            uint32_t peeked = (peekBuf >> (3U - codeSize)) & ((1U << codeSize) - 1U);
            if (peeked == codeVal)
            {
                br.skipBits(codeSize);
                return runVal;
            }
        }
        br.skipBits(1U);
        return 0U;
    }

    // zerosLeft > 6: Table 9-10 row 7+ uses prefix coding
    // VLC: 0..6 have 3-bit codes, 7+ use leading-zeros prefix
    uint32_t tableOffset = cRunBeforeIndex[6U]; // zerosLeft=7 table
    uint32_t peekBuf = br.peekBits(11U);

    for (uint32_t runVal = 0U; runVal < 15U && runVal <= zerosLeft; ++runVal)
    {
        uint32_t idx = tableOffset + runVal;
        if (idx >= 42U) break;

        uint8_t codeSize = cRunBeforeSize[idx];
        uint8_t codeVal  = cRunBeforeCode[idx];

        if (codeSize > 11U) break;

        uint32_t peeked = (peekBuf >> (11U - codeSize)) & ((1U << codeSize) - 1U);
        if (peeked == codeVal)
        {
            br.skipBits(codeSize);
            return runVal;
        }
    }

    br.skipBits(1U);
    return 0U;
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
    block = ResidualBlock4x4{};

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
