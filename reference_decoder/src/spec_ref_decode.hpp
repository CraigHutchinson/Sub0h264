/** Spec-only I-frame Decode Orchestration
 *
 *  ITU-T H.264 Section 7.3.5 (Macroblock layer)
 *  Wires together CABAC parsing, dequant, IDCT, and intra prediction
 *  to decode a full I-frame from an H.264 bitstream.
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef CROG_SUB0H264_SPEC_REF_DECODE_HPP
#define CROG_SUB0H264_SPEC_REF_DECODE_HPP

#include "spec_ref_cabac.hpp"
#include "spec_ref_cabac_init.hpp"
#include "spec_ref_cabac_parse.hpp"
#include "spec_ref_tables.hpp"
#include "spec_ref_transform.hpp"
#include "spec_ref_intra_pred.hpp"

#include "../../components/sub0h264/src/bitstream.hpp"
#include "../../components/sub0h264/src/frame.hpp"
#include "../../components/sub0h264/src/sps.hpp"
#include "../../components/sub0h264/src/pps.hpp"
#include "../../components/sub0h264/src/slice.hpp"
#include "../../components/sub0h264/src/annexb.hpp"

#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <memory>

namespace sub0h264 {
namespace spec_ref {

// ============================================================================
// Per-MB state used during decode
// ============================================================================

/// Macroblock type classification for our decoder.
enum class MbTypeClass : uint8_t
{
    I_4x4   = 0,
    I_16x16 = 1,
    I_PCM   = 2,
};

/// Per-macroblock decoded state (stored for neighbor context derivation).
struct MbInfo
{
    MbTypeClass type = MbTypeClass::I_4x4;
    uint32_t cbp = 0U;             ///< coded_block_pattern (luma bits 0-3, chroma bits 4-5)
    int32_t qp = 0;                ///< Current QP
    uint32_t chromaPredMode = 0U;  ///< intra_chroma_pred_mode
    int32_t qpDelta = 0;           ///< mb_qp_delta
    bool codedBlockFlag[26] = {};  ///< coded_block_flag per block (16 luma + 2 chroma DC + 8 chroma AC)
    uint8_t intra4x4PredMode[16] = {};  ///< Prediction modes for I_4x4
    uint32_t i16x16PredMode = 0U;  ///< Prediction mode for I_16x16

    bool available = false;
};

// ============================================================================
// I_4x4 Macroblock Decode -- ITU-T H.264 Section 7.3.5
// ============================================================================

/** Gather reference samples for Intra_4x4 prediction of a single 4x4 block.
 *
 *  ITU-T H.264 Section 8.3.1.2:
 *  Reference samples p[x,y] for x=-1..7, y=-1..3 from the frame.
 *
 *  @param frame     Current frame being decoded
 *  @param mbPixelX  X pixel coordinate of current MB's top-left
 *  @param mbPixelY  Y pixel coordinate of current MB's top-left
 *  @param blkX      X offset of 4x4 block within MB (0, 4, 8, or 12)
 *  @param blkY      Y offset of 4x4 block within MB (0, 4, 8, or 12)
 *  @param[out] above  8 pixels above the block (above[0..7] = p[0,-1]..p[7,-1])
 *  @param[out] left   4 pixels to the left (left[0..3] = p[-1,0]..p[-1,3])
 *  @param[out] topLeft  The p[-1,-1] pixel
 *  @param[out] hasAbove True if above samples exist
 *  @param[out] hasLeft  True if left samples exist
 */
inline void gatherIntra4x4Refs(const Frame& frame,
                                uint32_t mbPixelX, uint32_t mbPixelY,
                                uint32_t blkX, uint32_t blkY,
                                uint8_t above[8], uint8_t left[4],
                                uint8_t& topLeft,
                                bool& hasAbove, bool& hasLeft) noexcept
{
    uint32_t pixX = mbPixelX + blkX;
    uint32_t pixY = mbPixelY + blkY;

    hasAbove = (pixY > 0U);
    hasLeft = (pixX > 0U);

    // Fill above samples: p[0,-1]..p[7,-1]
    if (hasAbove) {
        const uint8_t* row = frame.yRow(pixY - 1U);
        for (uint32_t i = 0U; i < 8U; ++i) {
            uint32_t sx = pixX + i;
            if (sx < frame.width()) {
                above[i] = row[sx];
            } else {
                // Pad with last available
                above[i] = row[frame.width() - 1U];
            }
        }
    } else {
        std::memset(above, 128, 8);
    }

    // Fill left samples: p[-1,0]..p[-1,3]
    if (hasLeft) {
        for (uint32_t i = 0U; i < 4U; ++i) {
            left[i] = frame.y(pixX - 1U, pixY + i);
        }
    } else {
        std::memset(left, 128, 4);
    }

    // Top-left: p[-1,-1]
    if (hasAbove && hasLeft) {
        topLeft = frame.y(pixX - 1U, pixY - 1U);
    } else if (hasAbove) {
        topLeft = above[0];
    } else if (hasLeft) {
        topLeft = left[0];
    } else {
        topLeft = 128U;
    }
}

/** Get CBF context increment for coded_block_flag.
 *
 *  ITU-T H.264 Section 9.3.3.1.1.9:
 *  ctxIdxInc = condTermFlagA + 2*condTermFlagB
 *
 *  The full derivation depends on:
 *  1. Whether mbAddrN (neighbor MB) is available
 *  2. Whether the current MB is Intra or Inter
 *  3. Whether transBlockN (the specific neighboring transform block) is available
 *  4. Whether the neighbor is I_PCM
 *
 *  For I-slices (all Intra), the simplified rules are:
 *  - mbAddrN unavailable => condTermFlagN = 1 (Intra mode, spec bullet 2)
 *  - mbAddrN available, transBlockN unavailable (CBP bit=0) => condTermFlagN = 0
 *  - mbAddrN available, transBlockN available, I_PCM => condTermFlagN = 1
 *  - mbAddrN available, transBlockN available => coded_block_flag of transBlockN
 *
 *  This function is the simplified version for the common case where
 *  transBlockN availability has already been checked by the caller.
 *  The caller passes:
 *    - leftAvail/aboveAvail: true if transBlockN is available (MB exists AND CBP permits)
 *    - leftCbf/aboveCbf: the coded_block_flag of transBlockN when available
 *  When the neighbor MB doesn't exist at all, the caller should pass
 *  mbNotAvail=true to select condTerm=1 (Intra default).
 *
 *  @param leftAvail   True if left transBlockN is available
 *  @param leftCbf     coded_block_flag of left transBlockN (only used when leftAvail)
 *  @param aboveAvail  True if above transBlockN is available
 *  @param aboveCbf    coded_block_flag of above transBlockN (only used when aboveAvail)
 *  @param leftMbUnavail  True if the left MB itself doesn't exist (=> condTerm=1 for Intra)
 *  @param aboveMbUnavail True if the above MB itself doesn't exist (=> condTerm=1 for Intra)
 */
inline uint32_t getCbfCtxIncFull(bool leftAvail, bool leftCbf, bool leftMbUnavail,
                                  bool aboveAvail, bool aboveCbf, bool aboveMbUnavail) noexcept
{
    // ITU-T H.264 Section 9.3.3.1.1.9:
    // For Intra slices:
    //   mbAddrN not available => condTermFlagN = 1
    //   mbAddrN available, transBlockN not available => condTermFlagN = 0
    //   mbAddrN available, I_PCM => condTermFlagN = 1
    //   Otherwise => coded_block_flag of transBlockN
    uint32_t condA;
    if (leftMbUnavail) {
        condA = 1U; // Intra MB, neighbor MB not available
    } else if (!leftAvail) {
        condA = 0U; // MB available but transBlock not available (CBP=0)
    } else {
        condA = leftCbf ? 1U : 0U;
    }

    uint32_t condB;
    if (aboveMbUnavail) {
        condB = 1U; // Intra MB, neighbor MB not available
    } else if (!aboveAvail) {
        condB = 0U; // MB available but transBlock not available (CBP=0)
    } else {
        condB = aboveCbf ? 1U : 0U;
    }

    return condA + 2U * condB;
}

/** Simplified CBF context for cases where MB unavailability equals transBlock
 *  unavailability (chroma DC, I_16x16 DC). For Intra slices: unavailable MB => condTerm=1.
 */
inline uint32_t getCbfCtxInc(bool leftAvail, bool leftCbf,
                               bool aboveAvail, bool aboveCbf) noexcept
{
    // For Intra slices: mbAddrN not available => condTermFlagN = 1
    uint32_t condA = leftAvail ? (leftCbf ? 1U : 0U) : 1U;
    uint32_t condB = aboveAvail ? (aboveCbf ? 1U : 0U) : 1U;
    return condA + 2U * condB;
}

/** Decode a full I_4x4 macroblock.
 *
 *  @param engine    CABAC engine
 *  @param contexts  CABAC context array
 *  @param frame     Output frame
 *  @param mbInfo    Array of MB info for all MBs (for neighbor access)
 *  @param widthInMbs Width of frame in macroblocks
 *  @param mbX       MB x-coordinate
 *  @param mbY       MB y-coordinate
 *  @param qp        Current QP (updated with delta)
 */
inline void decodeI4x4Mb(CabacEngine& engine, CabacCtx* contexts,
                           Frame& frame, MbInfo* mbInfo,
                           uint32_t widthInMbs, uint32_t mbX, uint32_t mbY,
                           int32_t& qp, int32_t chromaQpOffset) noexcept
{
    uint32_t mbIdx = mbY * widthInMbs + mbX;
    MbInfo& cur = mbInfo[mbIdx];
    cur.type = MbTypeClass::I_4x4;
    cur.available = true;

    uint32_t mbPixelX = mbX * 16U;
    uint32_t mbPixelY = mbY * 16U;

    // -- Step 1: Decode 16 prediction modes ----------------------------------
    // ITU-T H.264 Section 8.3.1.1:
    // For each 4x4 block in spec scan order, decode prev_intra4x4_pred_mode_flag
    // and optionally rem_intra4x4_pred_mode.
    for (uint32_t blkIdx = 0U; blkIdx < 16U; ++blkIdx) {
        uint32_t prevFlag, remMode;
        cabacDecodeIntra4x4PredMode(engine, contexts, prevFlag, remMode);

        // ITU-T H.264 Section 8.3.1.1:
        // Compute most probable mode from neighbors A (left) and B (above)
        uint32_t blkXOff = cLuma4x4BlkX[blkIdx];
        uint32_t blkYOff = cLuma4x4BlkY[blkIdx];

        // Mode A: left neighbor's prediction mode
        uint32_t modeA = 2U; // DC default
        if (blkXOff > 0U) {
            // Left block is within this MB
            for (uint32_t j = 0U; j < 16U; ++j) {
                if (cLuma4x4BlkX[j] == blkXOff - 4U && cLuma4x4BlkY[j] == blkYOff) {
                    modeA = cur.intra4x4PredMode[j];
                    break;
                }
            }
        } else if (mbX > 0U) {
            MbInfo& leftMb = mbInfo[mbIdx - 1U];
            if (leftMb.available && leftMb.type == MbTypeClass::I_4x4) {
                // Right column of left MB at same y
                for (uint32_t j = 0U; j < 16U; ++j) {
                    if (cLuma4x4BlkX[j] == 12U && cLuma4x4BlkY[j] == blkYOff) {
                        modeA = leftMb.intra4x4PredMode[j];
                        break;
                    }
                }
            }
        }

        // Mode B: above neighbor's prediction mode
        uint32_t modeB = 2U; // DC default
        if (blkYOff > 0U) {
            // Above block is within this MB
            for (uint32_t j = 0U; j < 16U; ++j) {
                if (cLuma4x4BlkX[j] == blkXOff && cLuma4x4BlkY[j] == blkYOff - 4U) {
                    modeB = cur.intra4x4PredMode[j];
                    break;
                }
            }
        } else if (mbY > 0U) {
            MbInfo& aboveMb = mbInfo[(mbY - 1U) * widthInMbs + mbX];
            if (aboveMb.available && aboveMb.type == MbTypeClass::I_4x4) {
                // Bottom row of above MB at same x
                for (uint32_t j = 0U; j < 16U; ++j) {
                    if (cLuma4x4BlkX[j] == blkXOff && cLuma4x4BlkY[j] == 12U) {
                        modeB = aboveMb.intra4x4PredMode[j];
                        break;
                    }
                }
            }
        }

        // ITU-T H.264 Section 8.3.1.1:
        // predIntra4x4PredMode = min(modeA, modeB)
        uint32_t predMode = std::min(modeA, modeB);

        if (prevFlag == 1U) {
            cur.intra4x4PredMode[blkIdx] = static_cast<uint8_t>(predMode);
        } else {
            // ITU-T H.264 Section 8.3.1.1:
            // if rem < predMode: mode = rem
            // else: mode = rem + 1
            if (remMode < predMode) {
                cur.intra4x4PredMode[blkIdx] = static_cast<uint8_t>(remMode);
            } else {
                cur.intra4x4PredMode[blkIdx] = static_cast<uint8_t>(remMode + 1U);
            }
        }
    }

    // -- Step 2: Decode chroma prediction mode --------------------------------
    {
        uint32_t chromaCtxA = 0U;
        uint32_t chromaCtxB = 0U;
        if (mbX > 0U && mbInfo[mbIdx - 1U].available) {
            chromaCtxA = (mbInfo[mbIdx - 1U].chromaPredMode != 0U) ? 1U : 0U;
        }
        if (mbY > 0U && mbInfo[(mbY - 1U) * widthInMbs + mbX].available) {
            chromaCtxB = (mbInfo[(mbY - 1U) * widthInMbs + mbX].chromaPredMode != 0U) ? 1U : 0U;
        }
        cur.chromaPredMode = cabacDecodeIntraChromaMode(engine, contexts, chromaCtxA, chromaCtxB);
    }

    // -- Step 3: Decode CBP --------------------------------------------------
    {
        uint32_t leftCbp = 0U;
        uint32_t aboveCbp = 0U;
        bool leftAvail = (mbX > 0U && mbInfo[mbIdx - 1U].available);
        bool aboveAvail = (mbY > 0U && mbInfo[(mbY - 1U) * widthInMbs + mbX].available);

        if (leftAvail) {
            // FIX: Use actual stored CBP for all MB types, not hardcoded 0x2F
            leftCbp = mbInfo[mbIdx - 1U].cbp;
        }
        if (aboveAvail) {
            aboveCbp = mbInfo[(mbY - 1U) * widthInMbs + mbX].cbp;
        }

        cur.cbp = cabacDecodeCbp(engine, contexts, leftCbp, aboveCbp, leftAvail, aboveAvail);
    }

    // -- Step 4: Decode QP delta ---------------------------------------------
    bool hasNonZeroCbp = (cur.cbp != 0U);
    if (hasNonZeroCbp) {
        // Determine if previous MB had non-zero delta
        bool prevHadDelta = false;
        if (mbX > 0U || mbY > 0U) {
            uint32_t prevIdx = (mbX > 0U) ? mbIdx - 1U : ((mbY - 1U) * widthInMbs + widthInMbs - 1U);
            if (mbInfo[prevIdx].available) {
                prevHadDelta = (mbInfo[prevIdx].qpDelta != 0);
            }
        }
        cur.qpDelta = cabacDecodeMbQpDelta(engine, contexts, prevHadDelta);
        qp += cur.qpDelta;
        // Wrap QP into valid range
        if (qp < 0) qp += 52;
        if (qp > 51) qp -= 52;
    } else {
        cur.qpDelta = 0;
    }
    cur.qp = qp;

    // -- Step 5: Decode and reconstruct 16 luma 4x4 blocks -------------------
    for (uint32_t blkIdx = 0U; blkIdx < 16U; ++blkIdx) {
        uint32_t blkXOff = cLuma4x4BlkX[blkIdx];
        uint32_t blkYOff = cLuma4x4BlkY[blkIdx];

        // 5a. Generate prediction
        uint8_t above[8];
        uint8_t left[4];
        uint8_t topLeft;
        bool hasAbove, hasLeft;
        gatherIntra4x4Refs(frame, mbPixelX, mbPixelY, blkXOff, blkYOff,
                           above, left, topLeft, hasAbove, hasLeft);

        uint8_t predBlock[16];
        intra4x4Predict(predBlock, above, left, topLeft,
                        cur.intra4x4PredMode[blkIdx], hasAbove, hasLeft);

        // 5b. Decode residual coefficients
        int16_t scanCoeffs[16] = {};
        bool hasCoded = false;

        // Check if this block's 8x8 parent has coded coefficients
        uint32_t blk8x8Idx = (blkYOff / 8U) * 2U + (blkXOff / 8U);
        if ((cur.cbp >> blk8x8Idx) & 1U) {
            // ITU-T H.264 Section 9.3.3.1.1.9: coded_block_flag context
            // For ctxBlockCat=2 (luma 4x4), transBlockN is available when:
            //   mbAddrN available, not skipped, not I_PCM, AND
            //   (CodedBlockPatternLuma >> (luma4x4BlkIdxN >> 2)) & 1 != 0
            //
            // condTermFlagN:
            //   MB unavailable => 1 (Intra mode)
            //   MB available, transBlock unavailable => 0
            //   MB available, I_PCM => 1
            //   Otherwise => coded_block_flag of transBlockN

            bool leftCbf = false;
            bool leftTransAvail = false;
            bool leftMbUnavail = true;

            // Left 4x4 block
            if (blkXOff > 0U) {
                // Within this MB -- MB is always available, check CBP for 8x8 group
                leftMbUnavail = false;
                uint32_t leftBlkX = blkXOff - 4U;
                uint32_t left8x8 = (blkYOff / 8U) * 2U + (leftBlkX / 8U);
                if ((cur.cbp >> left8x8) & 1U) {
                    leftTransAvail = true;
                    for (uint32_t j = 0U; j < 16U; ++j) {
                        if (cLuma4x4BlkX[j] == leftBlkX && cLuma4x4BlkY[j] == blkYOff) {
                            leftCbf = cur.codedBlockFlag[j];
                            break;
                        }
                    }
                }
            } else if (mbX > 0U && mbInfo[mbIdx - 1U].available) {
                leftMbUnavail = false;
                MbInfo& leftMb = mbInfo[mbIdx - 1U];
                // Neighbor block is at x=12 in the left MB
                // Find its luma4x4BlkIdx and check CBP
                uint32_t neighborBlkX = 12U;
                uint32_t neighbor8x8 = (blkYOff / 8U) * 2U + (neighborBlkX / 8U);
                if ((leftMb.cbp >> neighbor8x8) & 1U) {
                    leftTransAvail = true;
                    for (uint32_t j = 0U; j < 16U; ++j) {
                        if (cLuma4x4BlkX[j] == neighborBlkX && cLuma4x4BlkY[j] == blkYOff) {
                            leftCbf = leftMb.codedBlockFlag[j];
                            break;
                        }
                    }
                }
            }

            bool aboveCbf = false;
            bool aboveTransAvail = false;
            bool aboveMbUnavail = true;

            // Above 4x4 block
            if (blkYOff > 0U) {
                aboveMbUnavail = false;
                uint32_t aboveBlkY = blkYOff - 4U;
                uint32_t above8x8 = (aboveBlkY / 8U) * 2U + (blkXOff / 8U);
                if ((cur.cbp >> above8x8) & 1U) {
                    aboveTransAvail = true;
                    for (uint32_t j = 0U; j < 16U; ++j) {
                        if (cLuma4x4BlkX[j] == blkXOff && cLuma4x4BlkY[j] == aboveBlkY) {
                            aboveCbf = cur.codedBlockFlag[j];
                            break;
                        }
                    }
                }
            } else if (mbY > 0U && mbInfo[(mbY - 1U) * widthInMbs + mbX].available) {
                aboveMbUnavail = false;
                MbInfo& aboveMb = mbInfo[(mbY - 1U) * widthInMbs + mbX];
                uint32_t neighborBlkY = 12U;
                uint32_t neighbor8x8 = (neighborBlkY / 8U) * 2U + (blkXOff / 8U);
                if ((aboveMb.cbp >> neighbor8x8) & 1U) {
                    aboveTransAvail = true;
                    for (uint32_t j = 0U; j < 16U; ++j) {
                        if (cLuma4x4BlkX[j] == blkXOff && cLuma4x4BlkY[j] == neighborBlkY) {
                            aboveCbf = aboveMb.codedBlockFlag[j];
                            break;
                        }
                    }
                }
            }

            uint32_t cbfInc = getCbfCtxIncFull(leftTransAvail, leftCbf, leftMbUnavail,
                                                aboveTransAvail, aboveCbf, aboveMbUnavail);
            hasCoded = cabacDecodeResidual4x4(engine, contexts, scanCoeffs, 16U, 2U, cbfInc);
        }
        cur.codedBlockFlag[blkIdx] = hasCoded;

        // 5c. Zigzag reorder: scan order -> raster order
        int16_t rasterCoeffs[16] = {};
        if (hasCoded) {
            for (uint32_t k = 0U; k < 16U; ++k) {
                rasterCoeffs[cZigzag4x4[k]] = scanCoeffs[k];
            }
        }

        // 5d. Dequant
        if (hasCoded) {
            inverseQuantize4x4(rasterCoeffs, qp);
        }

        // 5e. IDCT + add prediction -> frame
        uint32_t pixX = mbPixelX + blkXOff;
        uint32_t pixY = mbPixelY + blkYOff;
        if (hasCoded) {
            inverseDct4x4AddPred(rasterCoeffs, predBlock, 4U,
                                 frame.yRow(pixY) + pixX, frame.yStride());
        } else {
            // No residual -- just copy prediction
            for (uint32_t row = 0U; row < 4U; ++row) {
                for (uint32_t col = 0U; col < 4U; ++col) {
                    frame.y(pixX + col, pixY + row) = predBlock[row * 4 + col];
                }
            }
        }
    }

    // -- Step 6: Decode chroma -----------------------------------------------
    uint32_t chromaCbp = (cur.cbp >> 4U) & 3U;
    int32_t qpC_cb = chromaQp(qp, chromaQpOffset);
    int32_t qpC_cr = qpC_cb;

    for (uint32_t comp = 0U; comp < 2U; ++comp) {
        // Chroma prediction (8x8)
        uint8_t chromaPred[64];
        uint8_t chromaAbove[8];
        uint8_t chromaLeft[8];
        uint8_t chromaTopLeft;
        bool chromaHasAbove = (mbPixelY > 0U);
        bool chromaHasLeft = (mbPixelX > 0U);

        uint32_t chromaPixX = mbX * 8U;
        uint32_t chromaPixY = mbY * 8U;

        auto getChroma = [&](uint32_t x, uint32_t y) -> uint8_t {
            return comp == 0U ? frame.u(x, y) : frame.v(x, y);
        };
        auto getChromaRow = [&](uint32_t y) -> const uint8_t* {
            return comp == 0U ? frame.uRow(y) : frame.vRow(y);
        };

        if (chromaHasAbove) {
            const uint8_t* row = getChromaRow(chromaPixY - 1U);
            for (uint32_t i = 0U; i < 8U; ++i)
                chromaAbove[i] = row[chromaPixX + i];
        } else {
            std::memset(chromaAbove, 128, 8);
        }

        if (chromaHasLeft) {
            for (uint32_t i = 0U; i < 8U; ++i)
                chromaLeft[i] = getChroma(chromaPixX - 1U, chromaPixY + i);
        } else {
            std::memset(chromaLeft, 128, 8);
        }

        if (chromaHasAbove && chromaHasLeft)
            chromaTopLeft = getChroma(chromaPixX - 1U, chromaPixY - 1U);
        else if (chromaHasAbove)
            chromaTopLeft = chromaAbove[0];
        else if (chromaHasLeft)
            chromaTopLeft = chromaLeft[0];
        else
            chromaTopLeft = 128U;

        intraChromaPredict(chromaPred, chromaAbove, chromaLeft, chromaTopLeft,
                           cur.chromaPredMode, chromaHasAbove, chromaHasLeft);

        // Chroma DC (if chromaCbp >= 1)
        int16_t chromaDc[4] = {};
        bool hasDc = false;
        if (chromaCbp >= 1U) {
            // Decode chroma DC block (ctxBlockCat=3, maxNumCoeff=4)
            uint32_t cbfIdx = 16U + comp;
            bool leftDcCbf = true;
            bool leftDcAvail = false;
            bool aboveDcCbf = true;
            bool aboveDcAvail = false;

            if (mbX > 0U && mbInfo[mbIdx - 1U].available) {
                leftDcCbf = mbInfo[mbIdx - 1U].codedBlockFlag[16U + comp];
                leftDcAvail = true;
            }
            if (mbY > 0U && mbInfo[(mbY - 1U) * widthInMbs + mbX].available) {
                aboveDcCbf = mbInfo[(mbY - 1U) * widthInMbs + mbX].codedBlockFlag[16U + comp];
                aboveDcAvail = true;
            }

            uint32_t cbfInc = getCbfCtxInc(leftDcAvail, leftDcCbf, aboveDcAvail, aboveDcCbf);
            hasDc = cabacDecodeResidual4x4(engine, contexts, chromaDc, 4U, 3U, cbfInc);
            cur.codedBlockFlag[cbfIdx] = hasDc;
        }

        if (hasDc) {
            // Inverse Hadamard 2x2
            inverseHadamard2x2(chromaDc);
            // Dequant chroma DC
            int32_t qpChroma = (comp == 0U) ? qpC_cb : qpC_cr;
            inverseQuantizeChromaDc(chromaDc, qpChroma);
        }

        // Chroma AC (if chromaCbp == 2)
        int16_t chromaAcCoeffs[4][16] = {};
        for (uint32_t blk = 0U; blk < 4U; ++blk) {
            chromaAcCoeffs[blk][0] = chromaDc[blk]; // DC coefficient

            if (chromaCbp >= 2U) {
                // Decode AC coefficients (ctxBlockCat=4, maxNumCoeff=15)
                int16_t acScanCoeffs[15] = {};
                uint32_t acCbfIdx = 18U + comp * 4U + blk;

                // CBF context for chroma AC -- ITU-T H.264 Section 9.3.3.1.1.1
                uint32_t chromaBlkX = cChroma4x4BlkX[blk];
                uint32_t chromaBlkY = cChroma4x4BlkY[blk];

                bool leftAcCbf = true;
                bool leftAcAvail = false;
                bool aboveAcCbf = true;
                bool aboveAcAvail = false;

                if (chromaBlkX > 0U) {
                    // Left chroma block within this MB
                    uint32_t leftBlk = blk - 1U; // For 2x2: block 0->none, 1->0, 2->none, 3->2
                    if (blk == 1U || blk == 3U) {
                        leftAcCbf = cur.codedBlockFlag[18U + comp * 4U + leftBlk];
                        leftAcAvail = true;
                    }
                } else if (mbX > 0U && mbInfo[mbIdx - 1U].available) {
                    // Right chroma block of left MB
                    uint32_t leftBlk = (chromaBlkY == 0U) ? 1U : 3U;
                    leftAcCbf = mbInfo[mbIdx - 1U].codedBlockFlag[18U + comp * 4U + leftBlk];
                    leftAcAvail = true;
                }

                if (chromaBlkY > 0U) {
                    uint32_t aboveBlk = blk - 2U;
                    if (blk >= 2U) {
                        aboveAcCbf = cur.codedBlockFlag[18U + comp * 4U + aboveBlk];
                        aboveAcAvail = true;
                    }
                } else if (mbY > 0U && mbInfo[(mbY - 1U) * widthInMbs + mbX].available) {
                    uint32_t aboveBlk = (chromaBlkX == 0U) ? 2U : 3U;
                    aboveAcCbf = mbInfo[(mbY - 1U) * widthInMbs + mbX].codedBlockFlag[18U + comp * 4U + aboveBlk];
                    aboveAcAvail = true;
                }

                uint32_t cbfInc = getCbfCtxInc(leftAcAvail, leftAcCbf, aboveAcAvail, aboveAcCbf);
                bool hasAc = cabacDecodeResidual4x4(engine, contexts, acScanCoeffs, 15U, 4U, cbfInc);
                cur.codedBlockFlag[acCbfIdx] = hasAc;

                if (hasAc) {
                    // Zigzag reorder AC: scan positions 0..14 map to raster positions 1..15
                    for (uint32_t k = 0U; k < 15U; ++k) {
                        chromaAcCoeffs[blk][cZigzag4x4[k + 1U]] = acScanCoeffs[k];
                    }
                }
            }

            // Dequant AC (position 0 already has DC from Hadamard)
            int32_t qpChroma = (comp == 0U) ? qpC_cb : qpC_cr;
            if (chromaCbp >= 2U) {
                // Dequant only AC positions (1..15), preserve DC
                int16_t tempDc = chromaAcCoeffs[blk][0];
                inverseQuantize4x4(chromaAcCoeffs[blk], qpChroma);
                chromaAcCoeffs[blk][0] = tempDc; // Restore DC
            }
        }

        // IDCT and add to prediction for each 4x4 chroma block
        for (uint32_t blk = 0U; blk < 4U; ++blk) {
            uint32_t bx = cChroma4x4BlkX[blk];
            uint32_t by = cChroma4x4BlkY[blk];

            uint8_t* chromaOut;
            uint32_t chromaStride;

            if (comp == 0U) {
                chromaOut = frame.uRow(chromaPixY + by) + chromaPixX + bx;
                chromaStride = frame.uvStride();
            } else {
                chromaOut = frame.vRow(chromaPixY + by) + chromaPixX + bx;
                chromaStride = frame.uvStride();
            }

            bool hasAnyCoeff = (chromaDc[blk] != 0);
            if (!hasAnyCoeff && chromaCbp >= 2U) {
                for (uint32_t k = 1U; k < 16U; ++k) {
                    if (chromaAcCoeffs[blk][k] != 0) { hasAnyCoeff = true; break; }
                }
            }

            if (hasAnyCoeff) {
                // Extract prediction sub-block (4x4 from 8x8 prediction)
                uint8_t predSub[16];
                for (uint32_t row = 0U; row < 4U; ++row)
                    for (uint32_t col = 0U; col < 4U; ++col)
                        predSub[row * 4 + col] = chromaPred[(by + row) * 8 + bx + col];

                inverseDct4x4AddPred(chromaAcCoeffs[blk], predSub, 4U,
                                     chromaOut, chromaStride);
            } else {
                // Just copy prediction
                for (uint32_t row = 0U; row < 4U; ++row)
                    for (uint32_t col = 0U; col < 4U; ++col)
                        chromaOut[row * chromaStride + col] = chromaPred[(by + row) * 8 + bx + col];
            }
        }
    }
}

// ============================================================================
// I_16x16 Macroblock Decode
// ============================================================================

/** Decode a full I_16x16 macroblock. */
inline void decodeI16x16Mb(CabacEngine& engine, CabacCtx* contexts,
                             Frame& frame, MbInfo* mbInfo,
                             uint32_t widthInMbs, uint32_t mbX, uint32_t mbY,
                             int32_t& qp,
                             uint32_t i16x16PredMode, uint32_t cbpLuma, uint32_t cbpChroma,
                             int32_t chromaQpOffset) noexcept
{
    uint32_t mbIdx = mbY * widthInMbs + mbX;
    MbInfo& cur = mbInfo[mbIdx];
    cur.type = MbTypeClass::I_16x16;
    cur.available = true;
    cur.i16x16PredMode = i16x16PredMode;
    cur.cbp = cbpLuma | (cbpChroma << 4U);

    uint32_t mbPixelX = mbX * 16U;
    uint32_t mbPixelY = mbY * 16U;

    // Chroma pred mode
    {
        uint32_t chromaCtxA = 0U, chromaCtxB = 0U;
        if (mbX > 0U && mbInfo[mbIdx - 1U].available)
            chromaCtxA = (mbInfo[mbIdx - 1U].chromaPredMode != 0U) ? 1U : 0U;
        if (mbY > 0U && mbInfo[(mbY - 1U) * widthInMbs + mbX].available)
            chromaCtxB = (mbInfo[(mbY - 1U) * widthInMbs + mbX].chromaPredMode != 0U) ? 1U : 0U;
        cur.chromaPredMode = cabacDecodeIntraChromaMode(engine, contexts, chromaCtxA, chromaCtxB);
    }

    // QP delta -- ITU-T H.264 Section 7.3.5
    // For I_16x16, QP delta is ALWAYS decoded (CBP is implicit and always nonzero
    // because DC coefficients are always present)
    {
        bool prevHadDelta = false;
        if (mbX > 0U || mbY > 0U) {
            uint32_t prevIdx = (mbX > 0U) ? mbIdx - 1U : ((mbY - 1U) * widthInMbs + widthInMbs - 1U);
            if (mbInfo[prevIdx].available)
                prevHadDelta = (mbInfo[prevIdx].qpDelta != 0);
        }
        cur.qpDelta = cabacDecodeMbQpDelta(engine, contexts, prevHadDelta);
        qp += cur.qpDelta;
        if (qp < 0) qp += 52;
        if (qp > 51) qp -= 52;
    }
    cur.qp = qp;

    // Generate 16x16 prediction
    uint8_t pred16x16[256];
    {
        bool hasAbove = (mbPixelY > 0U);
        bool hasLeft = (mbPixelX > 0U);
        uint8_t above16[16], left16[16];
        uint8_t topLeft16;

        if (hasAbove) {
            const uint8_t* row = frame.yRow(mbPixelY - 1U);
            for (uint32_t i = 0U; i < 16U; ++i)
                above16[i] = row[mbPixelX + i];
        } else {
            std::memset(above16, 128, 16);
        }
        if (hasLeft) {
            for (uint32_t i = 0U; i < 16U; ++i)
                left16[i] = frame.y(mbPixelX - 1U, mbPixelY + i);
        } else {
            std::memset(left16, 128, 16);
        }
        if (hasAbove && hasLeft)
            topLeft16 = frame.y(mbPixelX - 1U, mbPixelY - 1U);
        else
            topLeft16 = 128U;

        intra16x16Predict(pred16x16, above16, left16, topLeft16,
                          i16x16PredMode, hasAbove, hasLeft);
    }

    // Decode luma DC (ctxBlockCat=0, maxNumCoeff=16)
    // ITU-T H.264 Section 9.3.3.1.1.1: coded_block_flag context
    int16_t lumaDc[16] = {};
    {
        bool leftDcCbf = true, aboveDcCbf = true;
        bool leftAvail = false, aboveAvail = false;
        if (mbX > 0U && mbInfo[mbIdx - 1U].available) {
            leftAvail = true;
            // For I_16x16 luma DC (cat 0), the neighbor's CBF is from their luma DC
            // If the neighbor is also I_16x16, check their DC coded_block_flag
            // For simplicity and correctness, assume coded (condTerm=1) for cross-MB
            leftDcCbf = true;
        }
        if (mbY > 0U && mbInfo[(mbY - 1U) * widthInMbs + mbX].available) {
            aboveAvail = true;
            aboveDcCbf = true;
        }
        uint32_t cbfInc = getCbfCtxInc(leftAvail, leftDcCbf, aboveAvail, aboveDcCbf);
        int16_t scanDc[16] = {};
        cabacDecodeResidual4x4(engine, contexts, scanDc, 16U, 0U, cbfInc);

        // Zigzag reorder: scan order -> raster order for the 4x4 DC block
        for (uint32_t k = 0U; k < 16U; ++k) {
            lumaDc[cZigzag4x4[k]] = scanDc[k];
        }
    }

    // FIX: Use the spec's two-step approach: Hadamard then dequant
    // The previous combined approach had a spurious factor of 16 from scaling lists,
    // causing DC coefficients to be 16x too large/wrong.
    // ITU-T H.264 Sections 8.5.11.1 + 8.5.12.1
    int16_t dequantDc[16];
    inverseHadamardDequant4x4LumaDc(lumaDc, dequantDc, qp);
    std::memcpy(lumaDc, dequantDc, sizeof(lumaDc));

    // Decode and reconstruct 16 luma 4x4 blocks
    for (uint32_t blkIdx = 0U; blkIdx < 16U; ++blkIdx) {
        uint32_t blkXOff = cLuma4x4BlkX[blkIdx];
        uint32_t blkYOff = cLuma4x4BlkY[blkIdx];

        int16_t rasterCoeffs[16] = {};
        // Map block scan index to DC raster index:
        // The 4x4 DC matrix is in raster order (row*4+col), but blkIdx follows
        // the spec's 8x8-then-4x4 scan order. Compute the DC matrix position
        // from the block's pixel offset within the macroblock.
        // ITU-T H.264 Section 8.5.11.1
        uint32_t dcRow = blkYOff / 4U;
        uint32_t dcCol = blkXOff / 4U;
        rasterCoeffs[0] = lumaDc[dcRow * 4U + dcCol]; // DC from Hadamard

        if (cbpLuma & 0x0FU) {
            // Decode AC coefficients (ctxBlockCat=1, maxNumCoeff=15)
            int16_t acScanCoeffs[15] = {};
            bool leftCbf = true, aboveCbf = true;
            bool leftCbfAvail = false, aboveCbfAvail = false;

            // FIX: Proper CBF context for I_16x16 AC blocks
            if (blkXOff > 0U) {
                // Within MB
                for (uint32_t j = 0U; j < 16U; ++j) {
                    if (cLuma4x4BlkX[j] == blkXOff - 4U && cLuma4x4BlkY[j] == blkYOff) {
                        leftCbf = cur.codedBlockFlag[j];
                        leftCbfAvail = true;
                        break;
                    }
                }
            } else if (mbX > 0U && mbInfo[mbIdx - 1U].available) {
                leftCbfAvail = true;
                // FIX: Look up actual CBF from left MB's rightmost block at same Y
                MbInfo& leftMb = mbInfo[mbIdx - 1U];
                if (leftMb.type == MbTypeClass::I_16x16) {
                    // For I_16x16, find the block at x=12, same y
                    for (uint32_t j = 0U; j < 16U; ++j) {
                        if (cLuma4x4BlkX[j] == 12U && cLuma4x4BlkY[j] == blkYOff) {
                            leftCbf = leftMb.codedBlockFlag[j];
                            break;
                        }
                    }
                } else {
                    // I_4x4 neighbor: their block CBF at rightmost column
                    for (uint32_t j = 0U; j < 16U; ++j) {
                        if (cLuma4x4BlkX[j] == 12U && cLuma4x4BlkY[j] == blkYOff) {
                            leftCbf = leftMb.codedBlockFlag[j];
                            break;
                        }
                    }
                }
            }

            if (blkYOff > 0U) {
                for (uint32_t j = 0U; j < 16U; ++j) {
                    if (cLuma4x4BlkX[j] == blkXOff && cLuma4x4BlkY[j] == blkYOff - 4U) {
                        aboveCbf = cur.codedBlockFlag[j];
                        aboveCbfAvail = true;
                        break;
                    }
                }
            } else if (mbY > 0U && mbInfo[(mbY - 1U) * widthInMbs + mbX].available) {
                aboveCbfAvail = true;
                // FIX: Look up actual CBF from above MB's bottom block at same X
                MbInfo& aboveMb = mbInfo[(mbY - 1U) * widthInMbs + mbX];
                if (aboveMb.type == MbTypeClass::I_16x16) {
                    for (uint32_t j = 0U; j < 16U; ++j) {
                        if (cLuma4x4BlkX[j] == blkXOff && cLuma4x4BlkY[j] == 12U) {
                            aboveCbf = aboveMb.codedBlockFlag[j];
                            break;
                        }
                    }
                } else {
                    for (uint32_t j = 0U; j < 16U; ++j) {
                        if (cLuma4x4BlkX[j] == blkXOff && cLuma4x4BlkY[j] == 12U) {
                            aboveCbf = aboveMb.codedBlockFlag[j];
                            break;
                        }
                    }
                }
            }

            uint32_t cbfInc = getCbfCtxInc(leftCbfAvail, leftCbf, aboveCbfAvail, aboveCbf);
            bool hasAc = cabacDecodeResidual4x4(engine, contexts, acScanCoeffs, 15U, 1U, cbfInc);
            cur.codedBlockFlag[blkIdx] = hasAc;

            if (hasAc) {
                for (uint32_t k = 0U; k < 15U; ++k) {
                    rasterCoeffs[cZigzag4x4[k + 1U]] = acScanCoeffs[k];
                }
            }
        }

        // Dequant AC
        int16_t tempDc = rasterCoeffs[0];
        inverseQuantize4x4(rasterCoeffs, qp);
        rasterCoeffs[0] = tempDc;

        // IDCT + add prediction -> frame
        uint32_t pixX = mbPixelX + blkXOff;
        uint32_t pixY = mbPixelY + blkYOff;
        uint8_t predBlock[16];
        for (uint32_t row = 0U; row < 4U; ++row)
            for (uint32_t col = 0U; col < 4U; ++col)
                predBlock[row * 4 + col] = pred16x16[(blkYOff + row) * 16 + blkXOff + col];

        inverseDct4x4AddPred(rasterCoeffs, predBlock, 4U,
                             frame.yRow(pixY) + pixX, frame.yStride());
    }

    // Chroma
    int32_t qpC_cb = chromaQp(qp, chromaQpOffset);
    int32_t qpC_cr = qpC_cb;

    for (uint32_t comp = 0U; comp < 2U; ++comp) {
        uint8_t chromaPred[64];
        uint8_t chromaAbove[8], chromaLeft[8], chromaTL;
        bool chromaHA = (mbPixelY > 0U), chromaHL = (mbPixelX > 0U);
        uint32_t cx = mbX * 8U, cy = mbY * 8U;

        auto gc = [&](uint32_t x, uint32_t y) -> uint8_t {
            return comp == 0U ? frame.u(x, y) : frame.v(x, y);
        };
        auto gcr = [&](uint32_t y) -> const uint8_t* {
            return comp == 0U ? frame.uRow(y) : frame.vRow(y);
        };

        if (chromaHA) {
            auto r = gcr(cy - 1);
            for (uint32_t i = 0; i < 8; ++i) chromaAbove[i] = r[cx + i];
        } else {
            std::memset(chromaAbove, 128, 8);
        }
        if (chromaHL) {
            for (uint32_t i = 0; i < 8; ++i) chromaLeft[i] = gc(cx - 1, cy + i);
        } else {
            std::memset(chromaLeft, 128, 8);
        }
        chromaTL = (chromaHA && chromaHL) ? gc(cx - 1, cy - 1) : 128;

        intraChromaPredict(chromaPred, chromaAbove, chromaLeft, chromaTL,
                           cur.chromaPredMode, chromaHA, chromaHL);

        int16_t cDc[4] = {};
        bool hasDc = false;
        if (cbpChroma >= 1U) {
            uint32_t cbfIdx = 16U + comp;
            bool ldc = true, adc = true, la = false, aa = false;
            if (mbX > 0 && mbInfo[mbIdx - 1].available) {
                la = true;
                ldc = mbInfo[mbIdx - 1].codedBlockFlag[16 + comp];
            }
            if (mbY > 0 && mbInfo[(mbY - 1) * widthInMbs + mbX].available) {
                aa = true;
                adc = mbInfo[(mbY - 1) * widthInMbs + mbX].codedBlockFlag[16 + comp];
            }
            uint32_t ci = getCbfCtxInc(la, ldc, aa, adc);
            hasDc = cabacDecodeResidual4x4(engine, contexts, cDc, 4U, 3U, ci);
            cur.codedBlockFlag[cbfIdx] = hasDc;
        }
        if (hasDc) {
            inverseHadamard2x2(cDc);
            int32_t qc = (comp == 0) ? qpC_cb : qpC_cr;
            inverseQuantizeChromaDc(cDc, qc);
        }

        int16_t cac[4][16] = {};
        for (uint32_t b = 0; b < 4; ++b) {
            cac[b][0] = cDc[b];
            if (cbpChroma >= 2U) {
                int16_t as[15] = {};
                uint32_t ai = 18 + comp * 4 + b;

                // FIX: Proper chroma AC CBF context (was hardcoded to (false,true,false,true))
                uint32_t chromaBlkX = cChroma4x4BlkX[b];
                uint32_t chromaBlkY = cChroma4x4BlkY[b];
                bool leftAcCbf = true, leftAcAvail = false;
                bool aboveAcCbf = true, aboveAcAvail = false;

                if (chromaBlkX > 0U) {
                    if (b == 1U || b == 3U) {
                        leftAcCbf = cur.codedBlockFlag[18U + comp * 4U + b - 1U];
                        leftAcAvail = true;
                    }
                } else if (mbX > 0U && mbInfo[mbIdx - 1U].available) {
                    uint32_t leftBlk = (chromaBlkY == 0U) ? 1U : 3U;
                    leftAcCbf = mbInfo[mbIdx - 1U].codedBlockFlag[18U + comp * 4U + leftBlk];
                    leftAcAvail = true;
                }

                if (chromaBlkY > 0U) {
                    if (b >= 2U) {
                        aboveAcCbf = cur.codedBlockFlag[18U + comp * 4U + b - 2U];
                        aboveAcAvail = true;
                    }
                } else if (mbY > 0U && mbInfo[(mbY - 1U) * widthInMbs + mbX].available) {
                    uint32_t aboveBlk = (chromaBlkX == 0U) ? 2U : 3U;
                    aboveAcCbf = mbInfo[(mbY - 1U) * widthInMbs + mbX].codedBlockFlag[18U + comp * 4U + aboveBlk];
                    aboveAcAvail = true;
                }

                uint32_t ci = getCbfCtxInc(leftAcAvail, leftAcCbf, aboveAcAvail, aboveAcCbf);
                bool ha = cabacDecodeResidual4x4(engine, contexts, as, 15U, 4U, ci);
                cur.codedBlockFlag[ai] = ha;
                if (ha) {
                    for (uint32_t k = 0; k < 15; ++k)
                        cac[b][cZigzag4x4[k + 1]] = as[k];
                }
            }
            int32_t qc = (comp == 0) ? qpC_cb : qpC_cr;
            if (cbpChroma >= 2U) {
                int16_t td = cac[b][0];
                inverseQuantize4x4(cac[b], qc);
                cac[b][0] = td;
            }
        }

        for (uint32_t b = 0; b < 4; ++b) {
            uint32_t bx = cChroma4x4BlkX[b], by = cChroma4x4BlkY[b];
            uint8_t ps[16];
            for (uint32_t r = 0; r < 4; ++r)
                for (uint32_t c = 0; c < 4; ++c)
                    ps[r * 4 + c] = chromaPred[(by + r) * 8 + bx + c];
            uint8_t* co;
            uint32_t cs;
            if (comp == 0) {
                co = frame.uRow(cy + by) + cx + bx;
                cs = frame.uvStride();
            } else {
                co = frame.vRow(cy + by) + cx + bx;
                cs = frame.uvStride();
            }
            bool any = (cDc[b] != 0);
            if (!any && cbpChroma >= 2) {
                for (uint32_t k = 1; k < 16; ++k)
                    if (cac[b][k] != 0) { any = true; break; }
            }
            if (any)
                inverseDct4x4AddPred(cac[b], ps, 4, co, cs);
            else {
                for (uint32_t r = 0; r < 4; ++r)
                    for (uint32_t c = 0; c < 4; ++c)
                        co[r * cs + c] = ps[r * 4 + c];
            }
        }
    }
}

// ============================================================================
// Top-level IDR frame decoder
// ============================================================================

/** Decode a complete IDR I-frame from H.264 Annex-B data.
 *
 *  @param h264Data  Pointer to H.264 Annex-B byte stream
 *  @param size      Size in bytes
 *  @param frame     Output frame (allocated by this function)
 *  @return True on success
 */
inline bool decodeIdrFrame(const uint8_t* h264Data, uint32_t size, Frame& frame) noexcept
{
    // Parse NAL units
    std::vector<NalBounds> nalBounds;
    findNalUnits(h264Data, size, nalBounds);

    Sps spsArray[cMaxSpsCount] = {};
    Pps ppsArray[cMaxPpsCount] = {};
    const Sps* activeSps = nullptr;
    const Pps* activePps = nullptr;

    // First pass: parse SPS/PPS
    for (const auto& bounds : nalBounds) {
        NalUnit nal;
        if (!parseNalUnit(h264Data + bounds.offset, bounds.size, nal))
            continue;

        if (nal.type == NalType::Sps) {
            BitReader br(nal.rbspData.data(), static_cast<uint32_t>(nal.rbspData.size()));
            Sps sps;
            if (parseSps(br, sps) == Result::Ok) {
                spsArray[sps.spsId_] = sps;
            }
        } else if (nal.type == NalType::Pps) {
            BitReader br(nal.rbspData.data(), static_cast<uint32_t>(nal.rbspData.size()));
            Pps pps;
            if (parsePps(br, spsArray, pps) == Result::Ok) {
                ppsArray[pps.ppsId_] = pps;
            }
        }
    }

    // Second pass: find and decode IDR slice
    for (const auto& bounds : nalBounds) {
        NalUnit nal;
        if (!parseNalUnit(h264Data + bounds.offset, bounds.size, nal))
            continue;

        if (nal.type != NalType::SliceIdr)
            continue;

        BitReader br(nal.rbspData.data(), static_cast<uint32_t>(nal.rbspData.size()));

        // Parse slice header
        BitReader brPeek = br;
        brPeek.readUev(); // first_mb_in_slice
        brPeek.readUev(); // slice_type
        uint32_t ppsId = brPeek.readUev();

        if (ppsId >= cMaxPpsCount || !ppsArray[ppsId].valid_)
            return false;

        activePps = &ppsArray[ppsId];
        activeSps = &spsArray[activePps->spsId_];

        SliceHeader sh;
        if (parseSliceHeader(br, *activeSps, *activePps, true, nal.refIdc, sh) != Result::Ok)
            return false;

        // Allocate frame
        frame.allocate(activeSps->width(), activeSps->height());

        // Initialize CABAC
        br.alignToByte(); // Align to byte boundary before CABAC init
        CabacEngine engine;
        engine.init(br);

        // Initialize contexts
        int32_t sliceQpY = activePps->picInitQp_ + sh.sliceQpDelta_;
        auto contexts = std::make_unique<CabacCtx[]>(cNumCabacContexts);
        initCabacContexts(contexts.get(), sliceQpY);

        uint32_t widthInMbs = activeSps->widthInMbs_;
        uint32_t heightInMbs = activeSps->heightInMbs_;
        uint32_t totalMbs = widthInMbs * heightInMbs;

        auto mbInfoArray = std::make_unique<MbInfo[]>(totalMbs);

        int32_t currentQp = sliceQpY;

        // Decode each macroblock
        for (uint32_t mbAddr = 0U; mbAddr < totalMbs; ++mbAddr) {
            uint32_t mbX = mbAddr % widthInMbs;
            uint32_t mbY = mbAddr / widthInMbs;

            // Decode mb_type
            uint32_t ctxA = 0U, ctxB = 0U;
            if (mbX > 0U && mbInfoArray[mbAddr - 1U].available) {
                ctxA = (mbInfoArray[mbAddr - 1U].type != MbTypeClass::I_4x4) ? 1U : 0U;
            }
            if (mbY > 0U && mbInfoArray[(mbY - 1U) * widthInMbs + mbX].available) {
                ctxB = (mbInfoArray[(mbY - 1U) * widthInMbs + mbX].type != MbTypeClass::I_4x4) ? 1U : 0U;
            }

            uint32_t mbType = cabacDecodeMbTypeI(engine, contexts.get(), ctxA, ctxB);

            if (mbType == 0U) {
                // I_4x4
                decodeI4x4Mb(engine, contexts.get(), frame, mbInfoArray.get(),
                             widthInMbs, mbX, mbY, currentQp,
                             activePps->chromaQpIndexOffset_);
            } else if (mbType >= 1U && mbType <= 24U) {
                // I_16x16
                // Decode mb_type fields:
                // mb_type = 1 + 12*cbpLumaFlag + 4*cbpChroma + i16x16PredMode
                uint32_t val = mbType - 1U;
                uint32_t i16x16PredMode = val % 4U;
                val /= 4U;
                uint32_t cbpChroma = val % 3U;
                uint32_t cbpLumaFlag = val / 3U;
                uint32_t cbpLuma = cbpLumaFlag ? 0x0FU : 0U;

                decodeI16x16Mb(engine, contexts.get(), frame, mbInfoArray.get(),
                               widthInMbs, mbX, mbY, currentQp,
                               i16x16PredMode, cbpLuma, cbpChroma,
                               activePps->chromaQpIndexOffset_);
            } else if (mbType == 25U) {
                // I_PCM -- read raw samples
                // TODO: implement I_PCM decode
                return false;
            }

            // Check for end of slice
            if (mbAddr < totalMbs - 1U) {
                uint32_t endFlag = engine.decodeTerminate();
                if (endFlag) break; // Early end of slice
            }
        }

        return true; // Successfully decoded
    }

    return false; // No IDR slice found
}

} // namespace spec_ref
} // namespace sub0h264

#endif // CROG_SUB0H264_SPEC_REF_DECODE_HPP
