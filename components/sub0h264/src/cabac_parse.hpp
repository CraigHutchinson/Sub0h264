/** Sub0h264 — CABAC syntax element parsers
 *
 *  Decodes H.264 syntax elements using CABAC for High profile.
 *  Each function maps a syntax element to its binarization scheme
 *  and context model selection per ITU-T H.264 §9.3.2/9.3.3.
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef CROG_SUB0H264_CABAC_PARSE_HPP
#define CROG_SUB0H264_CABAC_PARSE_HPP

#include "cabac.hpp"
#include "tables.hpp"

#include <cstdint>

namespace sub0h264 {

// ── Context offsets for syntax elements — ITU-T H.264 Table 9-11 ────────

inline constexpr uint32_t cCtxMbTypeI       = 3U;
inline constexpr uint32_t cCtxMbTypeP       = 14U;
inline constexpr uint32_t cCtxMbSkipP       = 11U;
inline constexpr uint32_t cCtxSubMbTypeP    = 21U;
inline constexpr uint32_t cCtxRefIdx        = 54U;
inline constexpr uint32_t cCtxMvdX          = 40U;
inline constexpr uint32_t cCtxMvdY          = 47U;
inline constexpr uint32_t cCtxMbQpDelta     = 60U;
inline constexpr uint32_t cCtxIntraChroma   = 64U;
inline constexpr uint32_t cCtxCbpLuma       = 73U;
inline constexpr uint32_t cCtxCbpChroma     = 77U;
inline constexpr uint32_t cCtxCbf           = 85U;
inline constexpr uint32_t cCtxSigCoeff      = 105U;
inline constexpr uint32_t cCtxLastSigCoeff  = 166U;
inline constexpr uint32_t cCtxCoeffAbsLevel = 227U;
inline constexpr uint32_t cCtxTransform8x8  = 399U;
inline constexpr uint32_t cCtxPrevIntra4x4  = 68U;
inline constexpr uint32_t cCtxRemIntra4x4   = 69U;

// ── mb_skip_flag (P-slice) — §9.3.3.1.1 ────────────────────────────────

/** Decode mb_skip_flag for P-slice.
 *  @param engine  CABAC engine
 *  @param ctx     Context array (460 entries)
 *  @param leftSkip  True if left MB was skipped
 *  @param topSkip   True if top MB was skipped
 *  @return 1 if MB is skipped, 0 otherwise
 */
inline uint32_t cabacDecodeMbSkipP(CabacEngine& engine, CabacCtx* ctx,
                                    bool leftSkip, bool topSkip) noexcept
{
    uint32_t ctxInc = (leftSkip ? 0U : 1U) + (topSkip ? 0U : 1U);
    return engine.decodeBin(ctx[cCtxMbSkipP + ctxInc]);
}

// ── mb_type (I-slice) — §9.3.3.1.2 ─────────────────────────────────────

/** Decode mb_type for I-slice (CABAC).
 *  @return mb_type raw value [0=I_4x4, 1-24=I_16x16 variants, 25=I_PCM]
 */
inline uint32_t cabacDecodeMbTypeI(CabacEngine& engine, CabacCtx* ctx,
                                    bool leftIsIntra, bool topIsIntra) noexcept
{
    uint32_t ctxInc = (leftIsIntra ? 0U : 1U) + (topIsIntra ? 0U : 1U);

    // bin[0]: ctxInc
    if (engine.decodeBin(ctx[cCtxMbTypeI + ctxInc]) == 0U)
        return 0U; // I_4x4

    // Terminate check for I_PCM
    if (engine.decodeTerminate() == 1U)
        return 25U; // I_PCM

    // I_16x16 binarization — ITU-T H.264 Table 9-34:
    //   bin[1] (ctx+3): cbpChroma > 0
    //   bin[2] (ctx+4): cbpChroma == 2 (only if bin[1] == 1)
    //   bin[3] (ctx+5): cbpLuma > 0
    //   bin[4:5] (ctx+6, ctx+7): predMode (2 bins)
    uint32_t cbpChromaFlag = engine.decodeBin(ctx[cCtxMbTypeI + 3U]);
    uint32_t cbpChroma = 0U;
    if (cbpChromaFlag != 0U)
        cbpChroma = engine.decodeBin(ctx[cCtxMbTypeI + 4U]) == 0U ? 1U : 2U;

    uint32_t cbpLuma = engine.decodeBin(ctx[cCtxMbTypeI + 5U]);
    uint32_t predMode = (engine.decodeBin(ctx[cCtxMbTypeI + 6U]) << 1U) |
                         engine.decodeBin(ctx[cCtxMbTypeI + 7U]);

    // mb_type = 1 + predMode + cbpChroma*4 + cbpLuma*12
    return 1U + predMode + cbpChroma * 4U + cbpLuma * 12U;
}

// ── mb_type (P-slice, inter) — §9.3.3.1.2 ──────────────────────────────

/** Decode mb_type for P-slice (CABAC).
 *  @return mb_type raw: 0=P_L0_16x16, 1=P_L0_L0_16x8, 2=P_L0_L0_8x16,
 *          3=P_8x8, 4=P_8x8ref0, 5-30=Intra (offset by 5)
 */
inline uint32_t cabacDecodeMbTypeP(CabacEngine& engine, CabacCtx* ctx) noexcept
{
    // bin[0]
    if (engine.decodeBin(ctx[cCtxMbTypeP + 0U]) == 0U)
    {
        // P_L0_* types
        uint32_t bin1 = engine.decodeBin(ctx[cCtxMbTypeP + 1U]);
        if (bin1 == 0U)
            return 0U; // P_L0_16x16
        uint32_t bin2 = engine.decodeBin(ctx[cCtxMbTypeP + 2U]);
        if (bin2 == 0U)
            return 3U; // P_8x8
        return engine.decodeBin(ctx[cCtxMbTypeP + 3U]) == 0U ? 1U : 2U;
    }

    // Intra MB within P-slice — decode I-slice mb_type + offset
    return 5U + cabacDecodeMbTypeI(engine, ctx, true, true);
}

// ── coded_block_pattern — §9.3.3.1.4 ───────────────────────────────────

/** Decode coded_block_pattern for CABAC.
 *  @return CBP value (4 luma bits + 2 chroma bits)
 */
inline uint8_t cabacDecodeCbp(CabacEngine& engine, CabacCtx* ctx,
                               bool leftHasLumaCbp, bool topHasLumaCbp,
                               bool leftHasChromaCbp, bool topHasChromaCbp) noexcept
{
    // Luma CBP: 4 bins, one per 8x8 block
    uint8_t cbpLuma = 0U;
    for (uint32_t i = 0U; i < 4U; ++i)
    {
        // Simplified context: use left/top availability
        uint32_t ctxInc = 0U;
        if (i == 0U) ctxInc = (leftHasLumaCbp ? 0U : 1U) + (topHasLumaCbp ? 0U : 2U);
        else if (i == 1U) ctxInc = ((cbpLuma & 1U) ? 0U : 1U) + (topHasLumaCbp ? 0U : 2U);
        else if (i == 2U) ctxInc = (leftHasLumaCbp ? 0U : 1U) + ((cbpLuma & 1U) ? 0U : 2U);
        else ctxInc = ((cbpLuma & 4U) ? 0U : 1U) + ((cbpLuma & 2U) ? 0U : 2U);

        if (engine.decodeBin(ctx[cCtxCbpLuma + ctxInc]) == 1U)
            cbpLuma |= (1U << i);
    }

    // Chroma CBP: 2 bins
    uint8_t cbpChroma = 0U;
    {
        uint32_t ctxInc = (leftHasChromaCbp ? 1U : 0U) + (topHasChromaCbp ? 2U : 0U);
        if (engine.decodeBin(ctx[cCtxCbpChroma + ctxInc]) == 1U)
        {
            ctxInc = (leftHasChromaCbp ? 1U : 0U) + (topHasChromaCbp ? 2U : 0U);
            cbpChroma = engine.decodeBin(ctx[cCtxCbpChroma + ctxInc + 4U]) == 0U ? 1U : 2U;
        }
    }

    return cbpLuma | (cbpChroma << 4U);
}

// ── mb_qp_delta — §9.3.3.1.5 ───────────────────────────────────────────

/** Decode mb_qp_delta for CABAC. */
inline int32_t cabacDecodeMbQpDelta(CabacEngine& engine, CabacCtx* ctx,
                                     bool prevMbHadQpDelta) noexcept
{
    uint32_t ctxInc0 = prevMbHadQpDelta ? 1U : 0U;
    if (engine.decodeBin(ctx[cCtxMbQpDelta + ctxInc0]) == 0U)
        return 0;

    // Unary with sign: decode absolute value then sign
    uint32_t absVal = 1U;
    while (absVal < 52U)
    {
        if (engine.decodeBin(ctx[cCtxMbQpDelta + 2U + (absVal > 1U ? 1U : 0U)]) == 0U)
            break;
        ++absVal;
    }

    // Map to signed: odd→positive, even→negative
    // 1→+1, 2→-1, 3→+2, 4→-2, ...
    int32_t delta;
    if (absVal & 1U)
        delta = static_cast<int32_t>((absVal + 1U) >> 1U);
    else
        delta = -static_cast<int32_t>(absVal >> 1U);

    return delta;
}

// ── MVD (motion vector difference) — §9.3.3.1.7 ────────────────────────

/** Decode one MVD component (x or y) for CABAC.
 *  @param ctxOffset  cCtxMvdX or cCtxMvdY
 *  @param absMvdNeighbor  Sum of |mvd| of left + top neighbors
 */
inline int16_t cabacDecodeMvd(CabacEngine& engine, CabacCtx* ctx,
                               uint32_t ctxOffset, int32_t absMvdNeighbor) noexcept
{
    // Context selection for bin 0 based on neighbor magnitude
    uint32_t ctxInc;
    if (absMvdNeighbor < 3)       ctxInc = 0U;
    else if (absMvdNeighbor <= 32) ctxInc = 1U;
    else                           ctxInc = 2U;

    // Prefix: truncated unary (max 9)
    uint32_t prefix = 0U;
    if (engine.decodeBin(ctx[ctxOffset + ctxInc]) == 1U)
    {
        ++prefix;
        while (prefix < 9U)
        {
            uint32_t binCtx = (prefix < 4U) ? (prefix + 2U) : 6U;
            if (engine.decodeBin(ctx[ctxOffset + binCtx]) == 0U)
                break;
            ++prefix;
        }
    }

    int32_t absMvd;
    if (prefix < 9U)
    {
        absMvd = static_cast<int32_t>(prefix);
    }
    else
    {
        // Suffix: 3rd-order Exp-Golomb via bypass bins
        uint32_t k = 0U;
        while (engine.decodeBypass() == 1U && k < 16U)
            ++k;
        uint32_t suffix = ((1U << k) - 1U) + engine.decodeBypassBins(k);
        absMvd = static_cast<int32_t>(9U + suffix);
    }

    if (absMvd == 0)
        return 0;

    // Sign bit (bypass)
    uint32_t sign = engine.decodeBypass();
    return sign ? static_cast<int16_t>(-absMvd) : static_cast<int16_t>(absMvd);
}

// ── Residual coefficient decoding — §9.3.3.1.9 ─────────────────────────

/** Decode significant_coeff_flag + last_significant_coeff_flag + levels
 *  for one 4x4 block using CABAC.
 *
 *  @param engine      CABAC engine
 *  @param ctx         Context array
 *  @param[out] coeffs Output coefficients in scan order
 *  @param maxCoeff    Maximum number of coefficients (15 for AC, 16 for 4x4)
 *  @param ctxBlockCat Block category for context offset selection
 *  @return Number of non-zero coefficients
 */
/** Decode a 4x4 residual block via CABAC — §9.3.3.1.1.
 *  @param cbfCtxInc  Context increment for coded_block_flag (0-3).
 *                    Derived from neighbor NNZ per §9.3.3.1.1.3:
 *                    ctxInc = (nA != 0) + 2*(nB != 0) where nA=left, nB=top.
 *                    Pass 0 for DC blocks (cat 0,3) where cbf is not decoded.
 */
inline uint32_t cabacDecodeResidual4x4(CabacEngine& engine, CabacCtx* ctx,
                                        int16_t* coeffs, uint32_t maxCoeff,
                                        uint32_t ctxBlockCat,
                                        uint32_t cbfCtxInc = 0U) noexcept
{
    // coded_block_flag — §9.3.3.1.1.1 Table 9-34.
    // Decoded for ctxBlockCat 1,2,4 (AC blocks). NOT decoded for cat 0,3 (DC blocks).
    if (ctxBlockCat != 0U && ctxBlockCat != 3U)
    {
        uint32_t cbfCtx = cCtxCbf + ctxBlockCat * 4U + cbfCtxInc;
        if (engine.decodeBin(ctx[cbfCtx]) == 0U)
            return 0U; // coded_block_flag = 0: no coefficients
    }

    // Context offsets per block category — ITU-T H.264 Table 9-39/9-40/9-41.
    // NOT uniformly spaced: chroma DC has only 3 sig contexts, chroma AC starts at 152.
    static constexpr uint32_t cSigOffsets[5]   = {105, 120, 134, 149, 152};
    static constexpr uint32_t cLastOffsets[5]  = {166, 181, 195, 210, 213};
    static constexpr uint32_t cLevelOffsets[5] = {227, 237, 247, 257, 267};
    uint32_t sigOffset   = cSigOffsets[ctxBlockCat];
    uint32_t lastOffset  = cLastOffsets[ctxBlockCat];
    uint32_t levelOffset = cLevelOffsets[ctxBlockCat];

    // Decode significant coefficient map
    uint8_t sigMap[16] = {};
    uint32_t numSig = 0U;
    int32_t lastSigIdx = -1;

    for (uint32_t i = 0U; i < maxCoeff - 1U; ++i)
    {
        if (engine.decodeBin(ctx[sigOffset + i]) == 1U)
        {
            sigMap[i] = 1U;
            lastSigIdx = static_cast<int32_t>(i);
            ++numSig;

            // Check last_significant_coeff_flag
            if (engine.decodeBin(ctx[lastOffset + i]) == 1U)
                break;
        }
    }

    // §9.3.3.1.3: If loop exhausted without last_significant_coeff_flag=1,
    // position maxCoeff-1 is implicitly significant.
    if (lastSigIdx < static_cast<int32_t>(maxCoeff - 1U))
    {
        // Either: no significant flags found at all (numSig==0, and we never
        // broke out), or we found some but none was "last". In both cases,
        // if numSig==0 after the full loop, there are truly no coefficients.
        if (numSig == 0U)
            return 0U;
        // Otherwise: last position is implicitly significant
        sigMap[maxCoeff - 1U] = 1U;
        lastSigIdx = static_cast<int32_t>(maxCoeff - 1U);
        ++numSig;
    }

    // Decode coefficient levels (reverse scan order)
    uint32_t numT1 = 0U;
    uint32_t numLarger = 0U;

    for (int32_t i = lastSigIdx; i >= 0; --i)
    {
        if (!sigMap[i])
            continue;

        // coeff_abs_level_minus1: prefix (truncated unary)
        uint32_t ctxInc;
        if (numLarger > 0U)
            ctxInc = 0U;
        else
            ctxInc = (numT1 < 4U) ? (numT1 + 1U) : 4U;

        uint32_t prefix = 0U;
        if (engine.decodeBin(ctx[levelOffset + ctxInc]) == 1U)
        {
            ++prefix;
            uint32_t nextCtx = 5U + (numLarger > 0U ? 1U : 0U);
            if (nextCtx > 9U) nextCtx = 9U;
            while (prefix < 14U)
            {
                if (engine.decodeBin(ctx[levelOffset + nextCtx]) == 0U)
                    break;
                ++prefix;
            }
        }

        int32_t absLevel;
        if (prefix < 14U)
        {
            absLevel = static_cast<int32_t>(prefix) + 1;
        }
        else
        {
            // Suffix: Exp-Golomb k=0 via bypass
            uint32_t k = 0U;
            while (engine.decodeBypass() == 1U && k < 16U)
                ++k;
            uint32_t suffix = ((1U << k) - 1U) + engine.decodeBypassBins(k);
            absLevel = static_cast<int32_t>(14U + suffix) + 1;
        }

        // Sign (bypass)
        uint32_t sign = engine.decodeBypass();
        // Output in SCAN ORDER — caller applies zigzag reordering.
        coeffs[i] = sign ? static_cast<int16_t>(-absLevel) : static_cast<int16_t>(absLevel);

        // Update context tracking
        if (absLevel == 1)
            ++numT1;
        else
            ++numLarger;
    }

    return numSig;
}

// ── Intra prediction mode (CABAC) — §9.3.3.1.3 ────────────────────────

/** Decode prev_intra4x4_pred_mode_flag + rem_intra4x4_pred_mode. */
inline uint8_t cabacDecodeIntra4x4PredMode(CabacEngine& engine, CabacCtx* ctx) noexcept
{
    if (engine.decodeBin(ctx[cCtxPrevIntra4x4]) == 1U)
        return 0xFFU; // Use most probable mode

    // 3 fixed-length bypass bins for rem_intra4x4_pred_mode
    return static_cast<uint8_t>(engine.decodeBypassBins(3U));
}

/** Decode intra_chroma_pred_mode (CABAC). */
inline uint32_t cabacDecodeIntraChromaMode(CabacEngine& engine, CabacCtx* ctx,
                                            bool leftIsIntra, bool topIsIntra) noexcept
{
    uint32_t ctxInc = (leftIsIntra ? 0U : 1U) + (topIsIntra ? 0U : 1U);

    if (engine.decodeBin(ctx[cCtxIntraChroma + ctxInc]) == 0U)
        return 0U; // DC

    if (engine.decodeBin(ctx[cCtxIntraChroma + 3U]) == 0U)
        return 1U; // Horizontal

    return engine.decodeBin(ctx[cCtxIntraChroma + 3U]) == 0U ? 2U : 3U;
}

} // namespace sub0h264

#endif // CROG_SUB0H264_CABAC_PARSE_HPP
