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

// ── High profile 8x8 block context offsets — ITU-T H.264 Table 9-42 ───
//
// ctxBlockCat 5 (8x8 luma blocks) uses separate context ranges beyond the
// base 460 contexts. Offsets per ITU-T H.264 Table 9-42 (frame coding):

/// significant_coeff_flag for ctxBlockCat=5 — base context index.
/// 63 positions use non-linear mapping via cSigCtxInc8x8Frame.
/// ITU-T H.264 Table 9-42: ctxIdxOffset = 402 for frame coding.
inline constexpr uint32_t cCtxSigCoeff8x8      = 402U;

/// last_significant_coeff_flag for ctxBlockCat=5 — base context index.
/// 63 positions use non-linear mapping via cLastSigCtxInc8x8Frame.
/// ITU-T H.264 Table 9-42: ctxIdxOffset = 417 for frame coding.
inline constexpr uint32_t cCtxLastSigCoeff8x8   = 417U;

/// coeff_abs_level_minus1 for ctxBlockCat=5 — base context index.
/// ITU-T H.264 Table 9-42: ctxIdxOffset = 426.
inline constexpr uint32_t cCtxCoeffAbsLevel8x8  = 426U;

/// coded_block_flag for ctxBlockCat=5 — High profile extension range.
/// ITU-T H.264 Table 9-42: ctxIdxOffset = 1012 for 8x8 luma blocks.
/// Requires context array expanded to 1024 entries.
inline constexpr uint32_t cCtxCbf8x8            = 1012U;

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
 *  §9.3.3.1.2: condTermFlagN = 0 when neighbor is I_NxN (I_4x4),
 *  = 1 when neighbor is NOT I_NxN (I_16x16, inter, or unavailable).
 *  @param leftIsI4x4  true when left neighbor is I_NxN (I_4x4)
 *  @param topIsI4x4   true when top neighbor is I_NxN (I_4x4)
 *  @return mb_type raw value [0=I_4x4, 1-24=I_16x16 variants, 25=I_PCM]
 */
inline uint32_t cabacDecodeMbTypeI(CabacEngine& engine, CabacCtx* ctx,
                                    bool leftIsI4x4, bool topIsI4x4) noexcept
{
    // §9.3.3.1.2: ctxIdxInc = condTermFlagA + condTermFlagB
    // condTermFlagN = 0 when I_NxN, 1 otherwise
    uint32_t ctxInc = (leftIsI4x4 ? 0U : 1U) + (topIsI4x4 ? 0U : 1U);

    // bin[0]: ctxInc
    if (engine.decodeBin(ctx[cCtxMbTypeI + ctxInc]) == 0U)
        return 0U; // I_4x4

    // Terminate check for I_PCM
    if (engine.decodeTerminate() == 1U)
        return 25U; // I_PCM

    // I_16x16 binarization — ffmpeg decode_cabac_intra_mb_type:
    //   After bin0 and terminate, state pointer advances by 2 for I-slices.
    //   state = &cabac_state[3+2] = &cabac_state[5].
    //   bin: state[1]=ctx[6] cbpLuma, state[2]=ctx[7] cbpChroma,
    //        state[3]=ctx[8] cbpChroma2, state[4]=ctx[9] predMode0,
    //        state[5]=ctx[10] predMode1.
    //   ORDER: cbpLuma FIRST, then cbpChroma, then predMode.
    // I_16x16 suffix — verified against spec-only agent trace and ffmpeg:
    // After bin0 + terminate, for I-slices: state base advances by 2.
    // state = &cabac_state[cCtxMbTypeI + 2] (for I-slice, ctxInc was 0-2 for bin0)
    // Suffix bins from that base:
    //   state[1] = ctx[6]:  cbpLuma (12*bin for mb_type formula)
    //   state[2] = ctx[7]:  cbpChroma > 0
    //   state[3] = ctx[8]:  cbpChroma == 2 (if bin above was 1)
    //   state[4] = ctx[9]:  predMode bit 1
    //   state[5] = ctx[10]: predMode bit 0
    // Agent confirmed: ctx6=cbpLuma, ctx7=cbpChroma, ctx9=pred0, ctx10=pred1
    constexpr uint32_t base = cCtxMbTypeI + 3U; // = 6
    uint32_t cbpLuma = engine.decodeBin(ctx[base]);       // ctx[6]

    uint32_t cbpChromaFlag = engine.decodeBin(ctx[base + 1U]); // ctx[7]
    uint32_t cbpChroma = 0U;
    if (cbpChromaFlag != 0U)
        cbpChroma = engine.decodeBin(ctx[base + 2U]) == 0U ? 1U : 2U; // ctx[8]

    // §9.3.2.5: predMode uses FL binarization (2 bins) at ctx[9,10]
    // Context indices: ctxIdxOffset(3) + ctxIdxInc(6) = 9, +7 = 10
    // Confirmed by x264 encoder which passes ctx[3+6]=9, ctx[3+7]=10
    uint32_t predMode = (engine.decodeBin(ctx[base + 3U]) << 1U) | // ctx[9]
                          engine.decodeBin(ctx[base + 4U]);          // ctx[10]

    // mb_type = 1 + predMode + cbpChroma*4 + cbpLuma*12
    return 1U + predMode + cbpChroma * 4U + cbpLuma * 12U;
}

// ── mb_type (P-slice, inter) — §9.3.3.1.2 ──────────────────────────────

/** Decode mb_type for P-slice (CABAC).
 *  @param leftIsI4x4  true when left neighbor is I_NxN (for intra suffix context)
 *  @param topIsI4x4   true when top neighbor is I_NxN (for intra suffix context)
 *  @return mb_type raw: 0=P_L0_16x16, 1=P_L0_L0_16x8, 2=P_L0_L0_8x16,
 *          3=P_8x8, 4=P_8x8ref0, 5-30=Intra (offset by 5)
 */
inline uint32_t cabacDecodeMbTypeP(CabacEngine& engine, CabacCtx* ctx,
                                    bool leftIsI4x4, bool topIsI4x4) noexcept
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

    // Intra MB within P-slice — decode I-slice mb_type suffix §9.3.3.1.2
    return 5U + cabacDecodeMbTypeI(engine, ctx, leftIsI4x4, topIsI4x4);
}

// ── coded_block_pattern — §9.3.3.1.4 ───────────────────────────────────

/** Decode coded_block_pattern for CABAC — §9.3.3.1.1.4.
 *
 *  Each of the 4 luma bins uses condTermFlag derived from a SPECIFIC 8x8
 *  block of the left/top neighbor (not just "has any luma CBP").
 *
 *  Block layout: 0=TL, 1=TR, 2=BL, 3=BR.
 *  Block 0: left=leftCbp bit 1, top=topCbp bit 2
 *  Block 1: left=current bit 0,  top=topCbp bit 3
 *  Block 2: left=leftCbp bit 3, top=current bit 0
 *  Block 3: left=current bit 2,  top=current bit 1
 *
 *  condTermFlagN = 1 when neighbor block is NOT coded (cbp bit=0),
 *                  0 when coded (cbp bit=1) or unavailable (treat as coded).
 *
 *  @param leftLumaCbp  Left MB's 4-bit luma CBP (bits 0-3), or 0x0F if unavailable
 *  @param topLumaCbp   Top MB's 4-bit luma CBP (bits 0-3), or 0x0F if unavailable
 *  @param leftChromaCbp Left MB's chroma CBP (0-2), or 2 if unavailable
 *  @param topChromaCbp  Top MB's chroma CBP (0-2), or 2 if unavailable
 */
inline uint8_t cabacDecodeCbp(CabacEngine& engine, CabacCtx* ctx,
                               uint8_t leftLumaCbp, uint8_t topLumaCbp,
                               uint8_t leftChromaCbp, uint8_t topChromaCbp) noexcept
{
    // Luma CBP: 4 bins, one per 8x8 block — §9.3.3.1.1.4
    // condTermFlagN = (neighbor_cbp_bit == 0) ? 1 : 0
    uint8_t cbpLuma = 0U;

    // Block 0 (TL): A=left's bit 1, B=top's bit 2
    {
        uint32_t cA = (leftLumaCbp & 0x02U) ? 0U : 1U;
        uint32_t cB = (topLumaCbp  & 0x04U) ? 0U : 2U;
        if (engine.decodeBin(ctx[cCtxCbpLuma + cA + cB]) == 1U) cbpLuma |= 1U;
    }
    // Block 1 (TR): A=current's bit 0, B=top's bit 3
    {
        uint32_t cA = (cbpLuma     & 0x01U) ? 0U : 1U;
        uint32_t cB = (topLumaCbp  & 0x08U) ? 0U : 2U;
        if (engine.decodeBin(ctx[cCtxCbpLuma + cA + cB]) == 1U) cbpLuma |= 2U;
    }
    // Block 2 (BL): A=left's bit 3, B=current's bit 0
    {
        uint32_t cA = (leftLumaCbp & 0x08U) ? 0U : 1U;
        uint32_t cB = (cbpLuma     & 0x01U) ? 0U : 2U;
        if (engine.decodeBin(ctx[cCtxCbpLuma + cA + cB]) == 1U) cbpLuma |= 4U;
    }
    // Block 3 (BR): A=current's bit 2, B=current's bit 1
    {
        uint32_t cA = (cbpLuma & 0x04U) ? 0U : 1U;
        uint32_t cB = (cbpLuma & 0x02U) ? 0U : 2U;
        if (engine.decodeBin(ctx[cCtxCbpLuma + cA + cB]) == 1U) cbpLuma |= 8U;
    }

    // Chroma CBP: 2 bins — §9.3.3.1.1.4
    // condTermFlagN = (neighbor_chromaCbp > 0) ? 0 : 1
    uint8_t cbpChroma = 0U;
    {
        uint32_t cA = (leftChromaCbp > 0U) ? 0U : 1U;
        uint32_t cB = (topChromaCbp  > 0U) ? 0U : 2U;
        if (engine.decodeBin(ctx[cCtxCbpChroma + cA + cB]) == 1U)
        {
            // Second chroma bin: cbpChroma == 1 or 2
            cA = (leftChromaCbp > 1U) ? 0U : 1U;
            cB = (topChromaCbp  > 1U) ? 0U : 2U;
            cbpChroma = engine.decodeBin(ctx[cCtxCbpChroma + cA + cB + 4U]) == 0U ? 1U : 2U;
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
    // coded_block_flag — §7.3.5.3.3 + §9.3.3.1.1.1 Table 9-34.
    {
        uint32_t cbfCtx = cCtxCbf + ctxBlockCat * 4U + cbfCtxInc;
        if (engine.decodeBin(ctx[cbfCtx]) == 0U)
            return 0U;
    }

    // Context offsets per block category — ITU-T H.264 Table 9-39/9-40/9-41.
    // NOT uniformly spaced: chroma DC has only 3 sig contexts, chroma AC starts at 152.
    static constexpr uint32_t cSigOffsets[5]   = {105, 120, 134, 149, 152};
    static constexpr uint32_t cLastOffsets[5]  = {166, 181, 195, 210, 213};
    // §9.3.3.1.1.5 Table 9-41: coeff_abs_level_minus1 ctxIdxBlockCatOffset.
    // Cat 3 (chroma DC) has only 9 contexts (max increment=8), not 10.
    // So cat 4 offset = 30+9 = 39 (not 40).
    static constexpr uint32_t cLevelOffsets[5] = {227, 237, 247, 257, 266};
    uint32_t sigOffset   = cSigOffsets[ctxBlockCat];
    uint32_t lastOffset  = cLastOffsets[ctxBlockCat];
    uint32_t levelOffset = cLevelOffsets[ctxBlockCat];

    // Decode significant coefficient map — §9.3.3.1.3 residual_block_cabac()
    uint8_t sigMap[16] = {};
    uint32_t numSig = 0U;
    int32_t lastSigIdx = -1;
    bool foundLast = false;

    for (uint32_t i = 0U; i < maxCoeff - 1U; ++i)
    {
        if (engine.decodeBin(ctx[sigOffset + i]) == 1U)
        {
            sigMap[i] = 1U;
            lastSigIdx = static_cast<int32_t>(i);
            ++numSig;

            // Check last_significant_coeff_flag
            if (engine.decodeBin(ctx[lastOffset + i]) == 1U)
            {
                foundLast = true;
                break;
            }
        }
    }

    // §9.3.3.1.3: Position maxCoeff-1 is implicitly significant ONLY when
    // the loop exhausted without last_significant_coeff_flag=1.
    // If last_flag was set (foundLast=true), the last significant position
    // is already recorded in lastSigIdx — do NOT add the implicit position.
    if (!foundLast)
    {
        if (numSig == 0U)
            return 0U;
        // Loop exhausted: position maxCoeff-1 is implicitly significant
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
        // §9.3.3.1.3: bin 0 context selection per x264 state machine.
        //   node_ctx starts at 0; coeff_abs_level1_ctx = {1,2,3,4,0,0,0,0}
        //   When numLarger==0: ctxIdxInc = min(numT1+1, 4)  [nodes 0-3]
        //   When numLarger>0:  ctxIdxInc = 0                 [nodes 4-7]
        uint32_t ctxInc;
        if (numLarger > 0U)
            ctxInc = 0U;
        else
            ctxInc = (numT1 < 4U) ? (numT1 + 1U) : 4U;

        uint32_t prefix = 0U;
        if (engine.decodeBin(ctx[levelOffset + ctxInc]) == 1U)
        {
            ++prefix;
            // §9.3.3.1.3: bins k>=1 use ctxIdxInc from coeff_abs_levelgt1_ctx.
            //   When numLarger==0: ctxIdxInc = 5 (nodes 0-3 all map to 5)
            //   When numLarger>0:  ctxIdxInc = 5 + min(numLarger, 4)
            //   [nodes 4→6, 5→7, 6→8, 7→9]
            uint32_t maxInc = (ctxBlockCat == 3U) ? 3U : 4U;
            uint32_t nextCtx;
            if (numLarger > 0U)
                nextCtx = 5U + (numLarger < maxInc ? numLarger : maxInc);
            else
                nextCtx = 5U;
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

// ── 8x8 block context increment mapping — ITU-T H.264 Table 9-43 ──────

/// Context index increment mapping for significant_coeff_flag ctxBlockCat=5.
/// Non-linear mapping per ITU-T H.264 Table 9-43 (frame coded blocks).
/// Index i (0..62) maps to the ctxIdxInc added to cCtxSigCoeff8x8.
inline constexpr std::array<uint8_t, 63> cSigCtxInc8x8Frame = {
     0,  1,  2,  3,  4,  5,  5,  4,
     4,  3,  3,  4,  4,  4,  5,  5,
     4,  4,  4,  4,  3,  3,  6,  7,
     7,  7,  8,  9, 10,  9,  8,  7,
     7,  6, 11, 12, 13, 11,  6,  7,
     8,  9, 14, 10,  9,  8,  6, 11,
    12, 13, 11,  6,  9, 14, 10,  9,
    11, 12, 13, 11, 14, 10, 12,
};

/// Context index increment mapping for last_significant_coeff_flag ctxBlockCat=5.
/// Non-linear mapping per ITU-T H.264 Table 9-43 (frame coded blocks).
/// Index i (0..62) maps to the ctxIdxInc added to cCtxLastSigCoeff8x8.
inline constexpr std::array<uint8_t, 63> cLastSigCtxInc8x8Frame = {
    0, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2,
    3, 3, 3, 3, 3, 3, 3, 3,
    4, 4, 4, 4, 4, 4, 4, 4,
    5, 5, 5, 5, 6, 6, 6, 6,
    7, 7, 7, 7, 8, 8, 8,
};

// Compile-time spot-checks — ITU-T H.264 Table 9-43.
static_assert(cSigCtxInc8x8Frame[0]  ==  0U, "sig[0]=0 per Table 9-43");
static_assert(cSigCtxInc8x8Frame[6]  ==  5U, "sig[6]=5 per Table 9-43");
static_assert(cSigCtxInc8x8Frame[22] ==  6U, "sig[22]=6 per Table 9-43");
static_assert(cSigCtxInc8x8Frame[42] == 14U, "sig[42]=14 per Table 9-43");
static_assert(cSigCtxInc8x8Frame[62] == 12U, "sig[62]=12 per Table 9-43");
static_assert(cLastSigCtxInc8x8Frame[0]  == 0U, "last[0]=0 per Table 9-43");
static_assert(cLastSigCtxInc8x8Frame[16] == 2U, "last[16]=2 per Table 9-43");
static_assert(cLastSigCtxInc8x8Frame[40] == 4U, "last[40]=4 per Table 9-43");
static_assert(cLastSigCtxInc8x8Frame[62] == 8U, "last[62]=8 per Table 9-43");

// ── 8x8 CABAC residual decode — ITU-T H.264 §9.3.3.1.1 ───────────────

/** Decode a 8x8 residual block via CABAC — High profile §9.3.3.1.1.
 *
 *  Uses ctxBlockCat=5 with non-linear context mapping per Table 9-43.
 *  Decodes coded_block_flag, 63 significant/last positions, and levels
 *  for a full 64-coefficient 8x8 block.
 *
 *  @param engine     CABAC engine
 *  @param ctx        Context array (1024 entries for High profile)
 *  @param[out] coeffs Output 64 coefficients in scan order (caller applies
 *                     zigzag reordering via cZigzag8x8)
 *  @param cbfCtxInc  Context increment for coded_block_flag (0-3).
 *                    Derived from neighbor NNZ per §9.3.3.1.1.3.
 *  @return Number of non-zero coefficients
 */
inline uint32_t cabacDecodeResidual8x8(CabacEngine& engine, CabacCtx* ctx,
                                        int16_t* coeffs, uint32_t cbfCtxInc = 0U) noexcept
{
    /// Maximum number of coefficients in an 8x8 block.
    static constexpr uint32_t cMaxCoeff8x8 = 64U;

    // §9.3.3.1.1.1: coded_block_flag is NOT decoded for ctxBlockCat=5 (8x8 luma).
    // Per the spec residual_block_cabac() pseudo-code, coded_block_flag applies
    // to categories 0-4 only. For 8x8 blocks, the significant map is always decoded.
    // (Confirmed by libavc ih264d_read_coeff8x8_cabac which skips cbf for 8x8.)
    (void)cbfCtxInc;

    // Decode significant coefficient map — §9.3.3.1.3 residual_block_cabac()
    // 8x8 blocks use non-linear context mapping per Table 9-43.
    uint8_t sigMap[cMaxCoeff8x8] = {};
    uint32_t numSig = 0U;
    int32_t lastSigIdx = -1;
    bool foundLast = false;

    for (uint32_t i = 0U; i < cMaxCoeff8x8 - 1U; ++i)
    {
        uint32_t sigCtx = cCtxSigCoeff8x8 + cSigCtxInc8x8Frame[i];
        if (engine.decodeBin(ctx[sigCtx]) == 1U)
        {
            sigMap[i] = 1U;
            lastSigIdx = static_cast<int32_t>(i);
            ++numSig;

            // Check last_significant_coeff_flag
            uint32_t lastCtx = cCtxLastSigCoeff8x8 + cLastSigCtxInc8x8Frame[i];
            if (engine.decodeBin(ctx[lastCtx]) == 1U)
            {
                foundLast = true;
                break;
            }
        }
    }

    // §9.3.3.1.3: Position maxCoeff-1 (63) is implicitly significant when
    // the loop exhausted without last_significant_coeff_flag=1.
    if (!foundLast)
    {
        if (numSig == 0U)
            return 0U;
        sigMap[cMaxCoeff8x8 - 1U] = 1U;
        lastSigIdx = static_cast<int32_t>(cMaxCoeff8x8 - 1U);
        ++numSig;
    }

    // Decode coefficient levels (reverse scan order) — §9.3.3.1.3.
    // coeff_abs_level_minus1 uses ctxBlockCat=5 offsets at cCtxCoeffAbsLevel8x8.
    uint32_t numT1 = 0U;
    uint32_t numLarger = 0U;

    for (int32_t i = lastSigIdx; i >= 0; --i)
    {
        if (!sigMap[i])
            continue;

        // coeff_abs_level_minus1: prefix (truncated unary)
        // §9.3.3.1.3: bin 0 context depends on trailing ones / larger counts.
        uint32_t ctxInc;
        if (numLarger > 0U)
            ctxInc = 0U;
        else
            ctxInc = (numT1 < 4U) ? (numT1 + 1U) : 4U;

        uint32_t prefix = 0U;
        if (engine.decodeBin(ctx[cCtxCoeffAbsLevel8x8 + ctxInc]) == 1U)
        {
            ++prefix;
            // §9.3.3.1.3: bins k>=1 use ctxIdxInc = 5 + min(numDecodAbsLevelGt1, 4)
            /// Maximum additional context increment for level suffix bins.
            static constexpr uint32_t cMaxLevelCtxInc8x8 = 4U;
            uint32_t nextCtx = 5U + (numLarger < cMaxLevelCtxInc8x8
                                     ? numLarger : cMaxLevelCtxInc8x8);
            while (prefix < 14U)
            {
                if (engine.decodeBin(ctx[cCtxCoeffAbsLevel8x8 + nextCtx]) == 0U)
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
            // Suffix: Exp-Golomb k=0 via bypass bins — §9.3.3.1.3.
            uint32_t k = 0U;
            while (engine.decodeBypass() == 1U && k < 16U)
                ++k;
            uint32_t suffix = ((1U << k) - 1U) + engine.decodeBypassBins(k);
            absLevel = static_cast<int32_t>(14U + suffix) + 1;
        }

        // Sign (bypass)
        uint32_t sign = engine.decodeBypass();
        // Output in SCAN ORDER — caller applies zigzag reordering via cZigzag8x8.
        coeffs[i] = sign ? static_cast<int16_t>(-absLevel)
                         : static_cast<int16_t>(absLevel);

        // Update context tracking
        if (absLevel == 1)
            ++numT1;
        else
            ++numLarger;
    }

    return numSig;
}

// ── Intra prediction mode (CABAC) — §9.3.3.1.3 ────────────────────────

/** Decode prev_intra4x4_pred_mode_flag + rem_intra4x4_pred_mode.
 *  §9.3.3.1.1.4: rem uses FL binarization. The H.264 spec Table 9-34
 *  assigns maxBinIdxCtx=2 and ctxIdxOffset=69, meaning these are
 *  context-coded bins at ctxIdx=69.
 *
 *  HOWEVER: ffmpeg actually uses bypass bins for rem_intra4x4_pred_mode
 *  in its CAVLC path (readBits(3)), and context-coded in CABAC path
 *  (get_cabac with state[69]). We use context-coded for CABAC.
 *
 *  Bit ordering is LSB-first: value = b0 | (b1<<1) | (b2<<2).
 */
inline uint8_t cabacDecodeIntra4x4PredMode(CabacEngine& engine, CabacCtx* ctx) noexcept
{
    if (engine.decodeBin(ctx[cCtxPrevIntra4x4]) == 1U)
        return 0xFFU; // Use most probable mode

    // 3 bins, LSB-first per ffmpeg: value = b0 | (b1<<1) | (b2<<2)
    // Context-coded at ctxIdx=69 per spec Table 9-34.
    uint32_t b0 = engine.decodeBin(ctx[cCtxRemIntra4x4]);
    uint32_t b1 = engine.decodeBin(ctx[cCtxRemIntra4x4]);
    uint32_t b2 = engine.decodeBin(ctx[cCtxRemIntra4x4]);
    return static_cast<uint8_t>(b0 | (b1 << 1U) | (b2 << 2U));
}

/** Decode intra_chroma_pred_mode (CABAC) — §9.3.3.1.1.7.
 *
 *  @param ctxInc  Pre-computed context increment [0-2] from neighbor chroma modes.
 *                 ctxInc = (chromaMode_left != 0 ? 1 : 0) + (chromaMode_top != 0 ? 1 : 0)
 */
inline uint32_t cabacDecodeIntraChromaMode(CabacEngine& engine, CabacCtx* ctx,
                                            uint32_t ctxInc) noexcept
{
    if (engine.decodeBin(ctx[cCtxIntraChroma + ctxInc]) == 0U)
        return 0U; // DC

    if (engine.decodeBin(ctx[cCtxIntraChroma + 3U]) == 0U)
        return 1U; // Horizontal

    return engine.decodeBin(ctx[cCtxIntraChroma + 3U]) == 0U ? 2U : 3U;
}

} // namespace sub0h264

#endif // CROG_SUB0H264_CABAC_PARSE_HPP
