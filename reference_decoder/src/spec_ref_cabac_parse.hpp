/** Spec-only CABAC Syntax Element Parsing
 *
 *  ITU-T H.264 Section 9.3.3.1 (Binarization and context derivation)
 *  Implements CABAC parsing for I-slice syntax elements.
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef CROG_SUB0H264_SPEC_REF_CABAC_PARSE_HPP
#define CROG_SUB0H264_SPEC_REF_CABAC_PARSE_HPP

#include "spec_ref_cabac.hpp"
#include "spec_ref_cabac_init.hpp"
#include "spec_ref_tables.hpp"

#include <cstdint>
#include <cstring>

namespace sub0h264 {
namespace spec_ref {

// ============================================================================
// Context Index Offsets -- ITU-T H.264 Tables 9-11, 9-34, 9-39, 9-40, 9-41
//
// These define the base ctxIdx for each syntax element category.
// ============================================================================

/// mb_type in I slices -- ctxIdxOffset = 3, uses ctxIdx 3..10
/// ITU-T H.264 Table 9-34 row for mb_type (SI/I slices)
inline constexpr uint32_t cCtxIdxOffsetMbTypeI = 3U;

/// coded_block_pattern luma -- ctxIdxOffset = 73, uses ctxIdx 73..76
/// ITU-T H.264 Table 9-34
inline constexpr uint32_t cCtxIdxOffsetCbpLuma = 73U;

/// coded_block_pattern chroma -- ctxIdxOffset = 77, uses ctxIdx 77..84
/// ITU-T H.264 Table 9-34
inline constexpr uint32_t cCtxIdxOffsetCbpChroma = 77U;

/// mb_qp_delta -- ctxIdxOffset = 60, uses ctxIdx 60..63
/// ITU-T H.264 Table 9-34
inline constexpr uint32_t cCtxIdxOffsetMbQpDelta = 60U;

/// prev_intra4x4_pred_mode_flag -- ctxIdxOffset = 68
/// ITU-T H.264 Table 9-34
inline constexpr uint32_t cCtxIdxOffsetPrevIntra4x4PredMode = 68U;

/// rem_intra4x4_pred_mode -- ctxIdxOffset = 69
/// ITU-T H.264 Table 9-34
inline constexpr uint32_t cCtxIdxOffsetRemIntra4x4PredMode = 69U;

/// intra_chroma_pred_mode -- ctxIdxOffset = 64, uses ctxIdx 64..67
/// ITU-T H.264 Table 9-34
inline constexpr uint32_t cCtxIdxOffsetIntraChromaPredMode = 64U;

// -- Residual context offsets per ctxBlockCat --------------------------------
// ITU-T H.264 Table 9-40: ctxIdxOffset for residual block syntax elements
//
// coded_block_flag:
//   ctxBlockCat 0 (Luma I16x16 DC): ctxIdxOffset = 85
//   ctxBlockCat 1 (Luma I16x16 AC): ctxIdxOffset = 89
//   ctxBlockCat 2 (Luma 4x4):       ctxIdxOffset = 93
//   ctxBlockCat 3 (Chroma DC):      ctxIdxOffset = 97
//   ctxBlockCat 4 (Chroma AC):      ctxIdxOffset = 101
inline constexpr uint32_t cCtxIdxOffsetCodedBlockFlag[5] = {
    85, 89, 93, 97, 101
};

// significant_coeff_flag:
//   ctxBlockCat 0 (I16x16 luma DC):  105, maxNumCoeff=16, range 105..119 (15 flags)
//   ctxBlockCat 1 (I16x16 luma AC):  120, maxNumCoeff=15, range 120..133 (14 flags)
//   ctxBlockCat 2 (luma 4x4):        134, maxNumCoeff=16, range 134..148 (15 flags)
//   ctxBlockCat 3 (chroma DC 2x2):   149, maxNumCoeff=4,  range 149..151 (3 flags)
//   ctxBlockCat 4 (chroma AC):       152, maxNumCoeff=15, range 152..165 (14 flags)
inline constexpr uint32_t cCtxIdxOffsetSigCoeff[5] = {
    105, 120, 134, 149, 152
};

// last_significant_coeff_flag:
//   ctxBlockCat 0: ctxIdxOffset = 166
//   ctxBlockCat 1: ctxIdxOffset = 181
//   ctxBlockCat 2: ctxIdxOffset = 195
//   ctxBlockCat 3: ctxIdxOffset = 210
//   ctxBlockCat 4: ctxIdxOffset = 213
inline constexpr uint32_t cCtxIdxOffsetLastSigCoeff[5] = {
    166, 181, 195, 210, 213
};

// coeff_abs_level_minus1:
//   ctxBlockCat 0: ctxIdxOffset = 227 (10 contexts: 227..236)
//   ctxBlockCat 1: ctxIdxOffset = 237 (10 contexts: 237..246)
//   ctxBlockCat 2: ctxIdxOffset = 247 (10 contexts: 247..256)
//   ctxBlockCat 3: ctxIdxOffset = 257 (10 contexts: 257..266)
//   ctxBlockCat 4: ctxIdxOffset = 266 (10 contexts: 266..275)
//   NOTE: cat 3 and cat 4 overlap at context 266 per spec Table 9-40
//   (ctxIdxBlockCatOffset for cat 4 is 39, not 40)
inline constexpr uint32_t cCtxIdxOffsetCoeffAbsLevel[5] = {
    227, 237, 247, 257, 266
};

/// Maximum number of coefficients per block category.
inline constexpr uint32_t cMaxNumCoeff[5] = { 16, 15, 16, 4, 15 };

// ============================================================================
// Decode mb_type for I slices -- ITU-T H.264 Section 9.3.3.1.1
// ============================================================================

/** Decode mb_type for I slices using CABAC.
 *
 *  ITU-T H.264 Section 9.3.3.1.1.3, Table 9-36:
 *  The mb_type for I slices is binarized as:
 *    0 = I_NxN (I_4x4 when transform_8x8_mode_flag=0)
 *    bin string "1" followed by terminate(1) = I_PCM (mb_type=25)
 *    bin string "1" + 2 bins for I_16x16 cbp-luma(0/15) + 2 bins for cbp-chroma + 4 bins for pred mode
 *
 *  @param engine    CABAC engine
 *  @param contexts  Context array
 *  @param ctxIdxA   Context increment from left neighbor (0 or 1)
 *  @param ctxIdxB   Context increment from above neighbor (0 or 1)
 *  @return mb_type value (0 = I_4x4, 1..24 = I_16x16 variants, 25 = I_PCM)
 */
inline uint32_t cabacDecodeMbTypeI(CabacEngine& engine, CabacCtx* contexts,
                                    uint32_t ctxIdxA, uint32_t ctxIdxB) noexcept
{
    // ITU-T H.264 Section 9.3.3.1.1.3, Table 9-36
    //
    // Binarization of mb_type in I slices:
    //   I_NxN (I_4x4): "0"
    //   I_PCM: "1" + terminate(1)
    //   I_16x16: "1" + terminate(0) + suffix
    //
    // Suffix structure (5 or 6 bins):
    //   Bin s0: cbpLumaFlag (FL, cMax=1)        ctxIdx = 3 + 3 = 6
    //   Bin s1: cbpChroma TU bin 0               ctxIdx = 3 + 4 = 7
    //   Bin s2: cbpChroma TU bin 1 (if s1=1)     ctxIdx = 3 + 5 = 8
    //   Bin s3: predMode FL bit 0 (high bit)     ctxIdx = 3 + 6 = 9
    //   Bin s4: predMode FL bit 1 (low bit)      ctxIdx = 3 + 7 = 10

    // First bin: ctxIdx = 3 + ctxIdxInc
    uint32_t ctxIdxInc = ctxIdxA + ctxIdxB;
    uint32_t firstBin = engine.decodeBin(contexts[cCtxIdxOffsetMbTypeI + ctxIdxInc]);

    if (firstBin == 0U) {
        return 0U; // I_NxN (I_4x4)
    }

    // Check terminate -- ITU-T H.264 Section 9.3.3.2.4
    uint32_t termBin = engine.decodeTerminate();
    if (termBin == 1U) {
        return 25U; // I_PCM
    }

    // I_16x16 suffix: decode bin-by-bin with correct ctxIdx

    // s0: cbpLumaFlag -- ctxIdx = 6
    uint32_t cbpLumaFlag = engine.decodeBin(contexts[cCtxIdxOffsetMbTypeI + 3U]);

    // s1: cbpChroma TU first bin -- ctxIdx = 7
    uint32_t chromaTuBin0 = engine.decodeBin(contexts[cCtxIdxOffsetMbTypeI + 4U]);

    uint32_t cbpChroma = 0U;
    if (chromaTuBin0 == 1U) {
        // s2: cbpChroma TU second bin -- ctxIdx = 8
        uint32_t chromaTuBin1 = engine.decodeBin(contexts[cCtxIdxOffsetMbTypeI + 5U]);
        cbpChroma = chromaTuBin1 ? 2U : 1U;
    }

    // s3: predMode high bit -- ctxIdx = 9
    uint32_t predBit0 = engine.decodeBin(contexts[cCtxIdxOffsetMbTypeI + 6U]);
    // s4: predMode low bit -- ctxIdx = 10
    uint32_t predBit1 = engine.decodeBin(contexts[cCtxIdxOffsetMbTypeI + 7U]);

    uint32_t i16x16PredMode = predBit0 * 2U + predBit1;

    // ITU-T H.264 Table 7-11:
    // mb_type = 1 + 12*cbpLumaFlag + 4*cbpChroma + i16x16PredMode
    return 1U + cbpLumaFlag * 12U + cbpChroma * 4U + i16x16PredMode;
}

// ============================================================================
// Decode prev_intra4x4_pred_mode_flag and rem_intra4x4_pred_mode
// ITU-T H.264 Section 9.3.3.1.1.4
// ============================================================================

/** Decode intra 4x4 prediction mode via CABAC.
 *
 *  @param engine    CABAC engine
 *  @param contexts  Context array
 *  @param[out] prevFlag  1 if prev_intra4x4_pred_mode_flag is set
 *  @param[out] remMode   rem_intra4x4_pred_mode (0..7) if prevFlag=0
 */
inline void cabacDecodeIntra4x4PredMode(CabacEngine& engine, CabacCtx* contexts,
                                         uint32_t& prevFlag, uint32_t& remMode) noexcept
{
    // ITU-T H.264 Section 9.3.3.1.1.4
    // prev_intra4x4_pred_mode_flag: FL, cMax=1, ctxIdx=68
    prevFlag = engine.decodeBin(contexts[cCtxIdxOffsetPrevIntra4x4PredMode]);

    if (prevFlag == 0U) {
        // rem_intra4x4_pred_mode: FL, cMax=7, 3 bins, ctxIdx=69 for all bins
        uint32_t b0 = engine.decodeBin(contexts[cCtxIdxOffsetRemIntra4x4PredMode]);
        uint32_t b1 = engine.decodeBin(contexts[cCtxIdxOffsetRemIntra4x4PredMode]);
        uint32_t b2 = engine.decodeBin(contexts[cCtxIdxOffsetRemIntra4x4PredMode]);
        remMode = b0 | (b1 << 1U) | (b2 << 2U);
    } else {
        remMode = 0U;
    }
}

// ============================================================================
// Decode intra_chroma_pred_mode -- ITU-T H.264 Section 9.3.3.1.1.7
// ============================================================================

/** Decode intra chroma prediction mode via CABAC.
 *
 *  Truncated unary binarization, cMax=3.
 *  @param engine    CABAC engine
 *  @param contexts  Context array
 *  @param ctxIdxA   Context increment from left (0 or 1)
 *  @param ctxIdxB   Context increment from above (0 or 1)
 *  @return Chroma prediction mode (0..3)
 */
inline uint32_t cabacDecodeIntraChromaMode(CabacEngine& engine, CabacCtx* contexts,
                                            uint32_t ctxIdxA, uint32_t ctxIdxB) noexcept
{
    // ITU-T H.264 Section 9.3.3.1.1.7
    // TU binarization, cMax=3
    // Bin 0: ctxIdx = 64 + ctxIdxInc (ctxIdxInc = condTermA + condTermB)
    uint32_t ctxIdxInc = ctxIdxA + ctxIdxB;
    uint32_t bin0 = engine.decodeBin(contexts[cCtxIdxOffsetIntraChromaPredMode + ctxIdxInc]);
    if (bin0 == 0U) return 0U;

    // Bin 1: ctxIdx = 64 + 3
    uint32_t bin1 = engine.decodeBin(contexts[cCtxIdxOffsetIntraChromaPredMode + 3U]);
    if (bin1 == 0U) return 1U;

    // Bin 2: ctxIdx = 64 + 3
    uint32_t bin2 = engine.decodeBin(contexts[cCtxIdxOffsetIntraChromaPredMode + 3U]);
    if (bin2 == 0U) return 2U;

    return 3U;
}

// ============================================================================
// Decode coded_block_pattern -- ITU-T H.264 Section 9.3.3.1.1.5
// ============================================================================

/** Decode coded_block_pattern for I slices via CABAC.
 *
 *  @param engine    CABAC engine
 *  @param contexts  Context array
 *  @param leftCbpY  CBP luma of left neighbor (or 0 if unavailable)
 *  @param aboveCbpY CBP luma of above neighbor (or 0 if unavailable)
 *  @param leftAvail True if left MB is available
 *  @param aboveAvail True if above MB is available
 *  @return CBP value (luma bits 0-3, chroma bits 4-5)
 */
inline uint32_t cabacDecodeCbp(CabacEngine& engine, CabacCtx* contexts,
                                uint32_t leftCbpY, uint32_t aboveCbpY,
                                bool leftAvail, bool aboveAvail) noexcept
{
    // ITU-T H.264 Section 9.3.3.1.1.5
    // coded_block_pattern luma: 4 bins, each with FL binarization
    // The 4 bins correspond to the 4 8x8 luma blocks in the order:
    //   bit 0: 8x8 block 0 (top-left)
    //   bit 1: 8x8 block 1 (top-right)
    //   bit 2: 8x8 block 2 (bottom-left)
    //   bit 3: 8x8 block 3 (bottom-right)
    //
    // Context derivation per bin:
    //   ctxIdxInc = condTermFlagA + 2*condTermFlagB
    //   where condTermFlagA = 1 if left 8x8 block has CBP=0, 0 if CBP=1
    //                        (inverted: condTerm = (neighborCBP_bit == 0) ? 1 : 0)
    //   For block 0: A = left MB bit 1, B = above MB bit 2
    //   For block 1: A = block 0 (current), B = above MB bit 3
    //   For block 2: A = left MB bit 3, B = block 0 (current)
    //   For block 3: A = block 2 (current), B = block 1 (current)

    uint32_t cbpY = 0U;

    auto getCondTerm = [](uint32_t cbp, uint32_t bit, bool available) -> uint32_t {
        if (!available) return 0U;
        return ((cbp >> bit) & 1U) == 0U ? 1U : 0U;
    };

    // Block 0: left=leftMB bit1, above=aboveMB bit2
    {
        uint32_t condA = getCondTerm(leftCbpY, 1U, leftAvail);
        uint32_t condB = getCondTerm(aboveCbpY, 2U, aboveAvail);
        uint32_t ctxIdxInc = condA + 2U * condB;
        uint32_t bin = engine.decodeBin(contexts[cCtxIdxOffsetCbpLuma + ctxIdxInc]);
        cbpY |= (bin << 0U);
    }

    // Block 1: left=current bit0, above=aboveMB bit3
    {
        uint32_t condA = ((cbpY >> 0U) & 1U) == 0U ? 1U : 0U;
        uint32_t condB = getCondTerm(aboveCbpY, 3U, aboveAvail);
        uint32_t ctxIdxInc = condA + 2U * condB;
        uint32_t bin = engine.decodeBin(contexts[cCtxIdxOffsetCbpLuma + ctxIdxInc]);
        cbpY |= (bin << 1U);
    }

    // Block 2: left=leftMB bit3, above=current bit0
    {
        uint32_t condA = getCondTerm(leftCbpY, 3U, leftAvail);
        uint32_t condB = ((cbpY >> 0U) & 1U) == 0U ? 1U : 0U;
        uint32_t ctxIdxInc = condA + 2U * condB;
        uint32_t bin = engine.decodeBin(contexts[cCtxIdxOffsetCbpLuma + ctxIdxInc]);
        cbpY |= (bin << 2U);
    }

    // Block 3: left=current bit2, above=current bit1
    {
        uint32_t condA = ((cbpY >> 2U) & 1U) == 0U ? 1U : 0U;
        uint32_t condB = ((cbpY >> 1U) & 1U) == 0U ? 1U : 0U;
        uint32_t ctxIdxInc = condA + 2U * condB;
        uint32_t bin = engine.decodeBin(contexts[cCtxIdxOffsetCbpLuma + ctxIdxInc]);
        cbpY |= (bin << 3U);
    }

    // coded_block_pattern chroma: TU binarization, cMax=2
    // Bin 0: ctxIdx = 77 + ctxIdxInc (A: left chroma CBP, B: above chroma CBP)
    // Bin 1: ctxIdx = 77 + 4 + ctxIdxInc
    uint32_t leftChromaCbp = leftAvail ? ((leftCbpY >> 4U) & 3U) : 0U;
    uint32_t aboveChromaCbp = aboveAvail ? ((aboveCbpY >> 4U) & 3U) : 0U;

    {
        uint32_t condA = (leftChromaCbp > 0U) ? 1U : 0U;
        uint32_t condB = (aboveChromaCbp > 0U) ? 1U : 0U;
        if (!leftAvail) condA = 0U;
        if (!aboveAvail) condB = 0U;
        uint32_t ctxIdxInc = condA + 2U * condB;
        uint32_t bin0 = engine.decodeBin(contexts[cCtxIdxOffsetCbpChroma + ctxIdxInc]);
        if (bin0 == 0U) {
            return cbpY; // chromaCBP = 0
        }
    }

    {
        uint32_t condA = (leftChromaCbp == 2U) ? 1U : 0U;
        uint32_t condB = (aboveChromaCbp == 2U) ? 1U : 0U;
        if (!leftAvail) condA = 0U;
        if (!aboveAvail) condB = 0U;
        uint32_t ctxIdxInc = condA + 2U * condB;
        uint32_t bin1 = engine.decodeBin(contexts[cCtxIdxOffsetCbpChroma + 4U + ctxIdxInc]);
        if (bin1 == 0U) {
            return cbpY | (1U << 4U); // chromaCBP = 1
        }
        return cbpY | (2U << 4U); // chromaCBP = 2
    }
}

// ============================================================================
// Decode mb_qp_delta -- ITU-T H.264 Section 9.3.3.1.1.6
// ============================================================================

/** Decode mb_qp_delta via CABAC.
 *
 *  Uses TU+EG0 binarization.
 *  @param engine    CABAC engine
 *  @param contexts  Context array
 *  @param prevMbHadNonZeroDelta  True if previous MB in slice had non-zero QP delta
 *  @return QP delta value (signed)
 */
inline int32_t cabacDecodeMbQpDelta(CabacEngine& engine, CabacCtx* contexts,
                                     bool prevMbHadNonZeroDelta) noexcept
{
    // ITU-T H.264 Section 9.3.3.1.1.6
    // Bin 0: ctxIdx = 60 + ctxIdxInc
    //   ctxIdxInc = 0 if prev MB had zero delta or is first MB, else 1
    uint32_t ctxIdxInc = prevMbHadNonZeroDelta ? 1U : 0U;
    uint32_t bin0 = engine.decodeBin(contexts[cCtxIdxOffsetMbQpDelta + ctxIdxInc]);
    if (bin0 == 0U) return 0;

    // Unary part: keep reading bins until 0 or we reach the limit
    // Bin 1: ctxIdx = 60 + 2
    // Bin 2+: ctxIdx = 60 + 3
    uint32_t bin1 = engine.decodeBin(contexts[cCtxIdxOffsetMbQpDelta + 2U]);
    if (bin1 == 0U) {
        // code = 1 => delta = 1 (positive)
        return 1;
    }

    int32_t code = 2;
    while (true) {
        uint32_t bin = engine.decodeBin(contexts[cCtxIdxOffsetMbQpDelta + 3U]);
        if (bin == 0U) break;
        ++code;
        if (code > 52) break; // Safety limit
    }

    // ITU-T H.264: map code to delta
    // code 0 => 0, code 1 => 1, code 2 => -1, code 3 => 2, code 4 => -2, ...
    int32_t delta = (code + 1) >> 1;
    if ((code & 1) == 0) delta = -delta;
    return delta;
}

// ============================================================================
// Decode residual block (coefficients) -- ITU-T H.264 Section 9.3.3.1.3
// ============================================================================

/** Decode a residual block using CABAC.
 *
 *  ITU-T H.264 Section 9.3.3.1.3:
 *  1. Decode coded_block_flag
 *  2. Decode significant_coeff_flag[i] for i = 0..maxNumCoeff-2
 *  3. For each significant, decode last_significant_coeff_flag[i]
 *  4. Decode coeff_abs_level_minus1 for each significant coefficient
 *  5. Decode coeff_sign_flag for each non-zero coefficient
 *
 *  @param engine       CABAC engine
 *  @param contexts     Context array (full 460 contexts)
 *  @param coeffs       Output: coefficient values in scan order
 *  @param maxNumCoeff  Maximum coefficients for this block category (4, 15, or 16)
 *  @param ctxBlockCat  Block category (0..4)
 *  @param cbfCtxInc    Context increment for coded_block_flag (from neighbors)
 *  @return True if any non-zero coefficients were decoded
 */
inline bool cabacDecodeResidual4x4(CabacEngine& engine, CabacCtx* contexts,
                                    int16_t* coeffs, uint32_t maxNumCoeff,
                                    uint32_t ctxBlockCat, uint32_t cbfCtxInc) noexcept
{
    std::memset(coeffs, 0, maxNumCoeff * sizeof(int16_t));

    // Step 1: coded_block_flag -- ITU-T H.264 Section 9.3.3.1.1.1
    uint32_t cbfCtxIdx = cCtxIdxOffsetCodedBlockFlag[ctxBlockCat] + cbfCtxInc;
    uint32_t codedBlockFlag = engine.decodeBin(contexts[cbfCtxIdx]);
    if (codedBlockFlag == 0U) {
        return false;
    }

    // Step 2 & 3: significant_coeff_flag and last_significant_coeff_flag
    // ITU-T H.264 Section 9.3.3.1.3
    uint8_t significantCoeff[16] = {};
    int32_t numCoeff = 0;
    int32_t lastCoeffIdx = -1;

    for (uint32_t i = 0U; i < maxNumCoeff - 1U; ++i) {
        // significant_coeff_flag: ctxIdx = offset + i
        uint32_t sigCtxIdx = cCtxIdxOffsetSigCoeff[ctxBlockCat] + i;
        uint32_t sig = engine.decodeBin(contexts[sigCtxIdx]);

        if (sig) {
            significantCoeff[i] = 1U;
            ++numCoeff;

            // last_significant_coeff_flag
            uint32_t lastCtxIdx = cCtxIdxOffsetLastSigCoeff[ctxBlockCat] + i;
            uint32_t last = engine.decodeBin(contexts[lastCtxIdx]);
            if (last) {
                lastCoeffIdx = static_cast<int32_t>(i);
                break;
            }
        }
    }

    // If no last flag was set, the last position is maxNumCoeff-1
    if (lastCoeffIdx < 0) {
        significantCoeff[maxNumCoeff - 1U] = 1U;
        ++numCoeff;
        lastCoeffIdx = static_cast<int32_t>(maxNumCoeff - 1U);
    }

    // Step 4 & 5: coeff_abs_level_minus1 and coeff_sign_flag
    // ITU-T H.264 Section 9.3.3.1.3
    // Process in REVERSE scan order (from last to first)
    //
    // Context derivation for coeff_abs_level_minus1 -- Section 9.3.3.1.1.10:
    // Uses a node-based state machine matching the spec's counter semantics.
    //
    // Key insight: numDecodAbsLevelEq1 in the spec is initialized to 1
    // (confirmed by both libavc and ffmpeg reference implementations).
    // This is because any significant coefficient implicitly has |level| >= 1.
    //
    // Bin 0 (first bin of TU prefix):
    //   If no level > 1 seen: ctxIdxInc = Min(numDecodAbsLevelEq1, 4)
    //   If level > 1 seen:    ctxIdxInc = 0
    //
    // Bins > 0 (subsequent prefix bins):
    //   ctxIdxInc = 5 + Min(numDecodAbsLevelGt1, 4)
    //
    // -- Verified against libavc ih264d_parse_cabac.c (u1_abs_level_equal1=1 init,
    //    u4_ctx_inc=0x51) and ffmpeg coeff_abs_level1_ctx[0]=1.
    uint32_t numDecodAbsLevelEq1 = 1U; // ITU-T H.264 §9.3.3.1.1.10: starts at 1
    uint32_t numDecodAbsLevelGt1 = 0U;

    for (int32_t i = lastCoeffIdx; i >= 0; --i) {
        if (!significantCoeff[i]) continue;

        // coeff_abs_level_minus1: prefix is TU (cMax=14) + suffix is EG0
        // Bin 0 context -- ITU-T H.264 Section 9.3.3.1.1.10, Table 9-41
        uint32_t ctxIdxIncBin0;
        if (numDecodAbsLevelGt1 != 0U) {
            ctxIdxIncBin0 = 0U; // After seeing level > 1, bin 0 uses ctxIdxInc=0
        } else {
            ctxIdxIncBin0 = std::min(numDecodAbsLevelEq1, 4U);
        }

        // Bins > 0 context -- ITU-T H.264 Section 9.3.3.1.1.10
        uint32_t ctxIdxIncBinGt0 = 5U + std::min(numDecodAbsLevelGt1, 4U);

        // Prefix: truncated unary max 14
        uint32_t levelPrefix = 0U;
        {
            uint32_t ctxIdx = cCtxIdxOffsetCoeffAbsLevel[ctxBlockCat] + ctxIdxIncBin0;
            uint32_t bin = engine.decodeBin(contexts[ctxIdx]);
            if (bin == 1U) {
                levelPrefix = 1U;
                uint32_t prefixCtxIdx = cCtxIdxOffsetCoeffAbsLevel[ctxBlockCat] + ctxIdxIncBinGt0;

                while (levelPrefix < 14U) {
                    bin = engine.decodeBin(contexts[prefixCtxIdx]);
                    if (bin == 0U) break;
                    ++levelPrefix;
                }
            }
        }

        // Suffix: Exp-Golomb order 0 via bypass bins (only if prefix == 14)
        uint32_t levelSuffix = 0U;
        if (levelPrefix >= 14U) {
            // EG0 bypass: leading 1s + 0 + suffix
            uint32_t k = 0U;
            while (engine.decodeBypass() == 1U) {
                ++k;
            }
            uint32_t suffix = 0U;
            for (uint32_t j = 0U; j < k; ++j) {
                suffix = (suffix << 1U) | engine.decodeBypass();
            }
            levelSuffix = (1U << k) - 1U + suffix;
        }

        uint32_t absLevel = levelPrefix + levelSuffix + 1U;

        // Sign flag (bypass)
        uint32_t sign = engine.decodeBypass();
        int16_t level = static_cast<int16_t>(sign ? -static_cast<int32_t>(absLevel) : static_cast<int32_t>(absLevel));
        coeffs[i] = level;

        // Update counters -- ITU-T H.264 Section 9.3.3.1.1.10
        if (absLevel == 1U) {
            ++numDecodAbsLevelEq1;
        } else {
            ++numDecodAbsLevelGt1;
        }
    }

    return true;
}

} // namespace spec_ref
} // namespace sub0h264

#endif // CROG_SUB0H264_SPEC_REF_CABAC_PARSE_HPP
