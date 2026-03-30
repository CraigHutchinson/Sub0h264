/** Sub0h264 — H.264 decoder pipeline
 *
 *  Wires together NAL parsing, parameter sets, slice headers,
 *  CAVLC entropy decoding, intra prediction, and inverse transform
 *  into a complete decode pipeline.
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef CROG_SUB0H264_DECODER_HPP
#define CROG_SUB0H264_DECODER_HPP

#include "annexb.hpp"
#include "bitstream.hpp"
#include "cabac.hpp"
#include "cabac_parse.hpp"
#include "cavlc.hpp"
#include "deblock.hpp"
#include "dpb.hpp"
#include "frame.hpp"
#include "inter_pred.hpp"
#include "intra_pred.hpp"
#include "motion.hpp"
#include "nal.hpp"
#include "param_sets.hpp"
#include "pps.hpp"
#include "slice.hpp"
#include "sps.hpp"
#include "tables.hpp"
#include "transform.hpp"
#include "sub0h264/sub0h264_types.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

namespace sub0h264 {

/// Decode status for a single frame.
enum class DecodeStatus : int32_t
{
    FrameDecoded =  1,   ///< A complete frame was decoded
    NeedMoreData =  0,   ///< NAL consumed, no frame yet (SPS/PPS/partial)
    Error        = -1,   ///< Decode error
};

/** Simplified H.264 decoder.
 *
 *  Currently supports I-frame (IDR) decoding only.
 *  Baseline profile, CAVLC entropy coding, max 640x480.
 */
class H264Decoder
{
public:
    H264Decoder() = default;

    /** Feed a complete Annex-B byte stream and decode all frames.
     *
     *  @param data   Annex-B byte stream (may contain multiple NALs/frames)
     *  @param size   Size in bytes
     *  @return Number of frames decoded, or -1 on error
     */
    int32_t decodeStream(const uint8_t* data, uint32_t size) noexcept
    {
        std::vector<NalBounds> bounds;
        findNalUnits(data, size, bounds);

        int32_t framesDecoded = 0;

        for (const auto& b : bounds)
        {
            NalUnit nal;
            if (!parseNalUnit(data + b.offset, b.size, nal))
                continue;

            DecodeStatus status = processNal(nal);
            if (status == DecodeStatus::FrameDecoded)
                ++framesDecoded;
        }

        return framesDecoded;
    }

    /** Process a single parsed NAL unit.
     *  @return DecodeStatus indicating result
     */
    DecodeStatus processNal(const NalUnit& nal) noexcept
    {
        BitReader br(nal.rbspData.data(), static_cast<uint32_t>(nal.rbspData.size()));

        switch (nal.type)
        {
        case NalType::Sps:
        {
            Sps sps;
            if (parseSps(br, sps) != Result::Ok)
                return DecodeStatus::Error;
            paramSets_.storeSps(sps);
            return DecodeStatus::NeedMoreData;
        }

        case NalType::Pps:
        {
            Pps pps;
            if (parsePps(br, paramSets_.spsArray(), pps) != Result::Ok)
                return DecodeStatus::Error;
            paramSets_.storePps(pps);
            return DecodeStatus::NeedMoreData;
        }

        case NalType::SliceIdr:
        case NalType::SliceNonIdr:
            return decodeSlice(br, nal);

        default:
            return DecodeStatus::NeedMoreData; // Ignore SEI, AUD, etc.
        }
    }

    /** @return Most recently decoded frame (nullptr if none). */
    const Frame* currentFrame() const noexcept
    {
        const Frame* dpbFrame = dpb_.currentFrame();
        if (dpbFrame && dpbFrame->isAllocated())
            return dpbFrame;
        return currentFrame_.isAllocated() ? &currentFrame_ : nullptr;
    }

    /** @return Mutable reference to parameter sets. */
    ParamSets& paramSets() noexcept { return paramSets_; }

    /** @return Number of frames decoded so far. */
    uint32_t frameCount() const noexcept { return frameCount_; }

private:
    ParamSets paramSets_;
    Frame currentFrame_;
    Dpb dpb_;
    uint32_t frameCount_ = 0U;
    bool dpbInitialized_ = false;

    // Per-frame MB context: non-zero coefficient counts for CAVLC context
    std::vector<uint8_t> nnzLuma_;    // [mbIdx * 16 + blkIdx]
    std::vector<uint8_t> nnzCb_;      // [mbIdx * 4 + blkIdx]
    std::vector<uint8_t> nnzCr_;      // [mbIdx * 4 + blkIdx]

    // Per-frame MV context for inter prediction
    std::vector<MbMotionInfo> mbMotion_;  // [mbIdx]

    // Per-MB intra 4x4 prediction modes for MPM derivation across MBs.
    // [mbIdx * 16 + blkIdx] — only valid for I_4x4 MBs.
    // For I_16x16 MBs, all entries set to DC(2) per spec §8.3.1.1.
    std::vector<uint8_t> mbIntra4x4Modes_;  // [mbIdx * 16 + blkIdx]

    // CABAC state
    CabacEngine cabacEngine_;
    std::array<CabacCtx, cNumCabacCtx> cabacCtx_ = {};
    std::vector<bool> mbIsSkip_;  // [mbIdx] — for CABAC skip context

    uint16_t widthInMbs_ = 0U;
    uint16_t heightInMbs_ = 0U;

    /** Get nC (CAVLC context) for a luma 4x4 block.
     *  nC = (leftNnz + topNnz + 1) >> 1
     */
    int32_t getLumaNc(uint32_t mbX, uint32_t mbY, uint32_t blkIdx) const noexcept
    {
        // Block position within MB (4x4 grid):
        //  0  1  2  3
        //  4  5  6  7
        //  8  9 10 11
        // 12 13 14 15
        uint32_t blkX = blkIdx & 3U;
        uint32_t blkY = blkIdx >> 2U;

        int32_t leftNnz = 0;
        int32_t topNnz = 0;
        bool hasLeft = false, hasTop = false;

        // Left neighbor
        if (blkX > 0U)
        {
            // Within same MB
            uint32_t leftBlk = blkIdx - 1U;
            leftNnz = nnzLuma_[(mbY * widthInMbs_ + mbX) * 16U + leftBlk];
            hasLeft = true;
        }
        else if (mbX > 0U)
        {
            // From left MB, rightmost column
            uint32_t leftBlk = blkY * 4U + 3U;
            leftNnz = nnzLuma_[(mbY * widthInMbs_ + mbX - 1U) * 16U + leftBlk];
            hasLeft = true;
        }

        // Top neighbor
        if (blkY > 0U)
        {
            // Within same MB
            uint32_t topBlk = blkIdx - 4U;
            topNnz = nnzLuma_[(mbY * widthInMbs_ + mbX) * 16U + topBlk];
            hasTop = true;
        }
        else if (mbY > 0U)
        {
            // From top MB, bottom row
            uint32_t topBlk = 12U + blkX;
            topNnz = nnzLuma_[((mbY - 1U) * widthInMbs_ + mbX) * 16U + topBlk];
            hasTop = true;
        }

        if (hasLeft && hasTop) return (leftNnz + topNnz + 1) >> 1;
        if (hasLeft) return leftNnz;
        if (hasTop) return topNnz;
        return 0;
    }

    /** Decode a slice (IDR or non-IDR). */
    DecodeStatus decodeSlice(BitReader& br, const NalUnit& nal) noexcept
    {
        bool isIdr = (nal.type == NalType::SliceIdr);

        // Peek at PPS ID to find the right parameter sets
        BitReader peekBr = br;
        peekBr.readUev(); // first_mb_in_slice
        peekBr.readUev(); // slice_type
        uint8_t ppsId = static_cast<uint8_t>(peekBr.readUev());

        const Pps* pps = paramSets_.getPps(ppsId);
        if (!pps) return DecodeStatus::Error;
        const Sps* sps = paramSets_.getSps(pps->spsId_);
        if (!sps) return DecodeStatus::Error;

        // Parse slice header
        SliceHeader sh;
        if (parseSliceHeader(br, *sps, *pps, isIdr, nal.refIdc, sh) != Result::Ok)
            return DecodeStatus::Error;

        // Initialize DPB and allocate context on first SPS use
        widthInMbs_ = sps->widthInMbs_;
        heightInMbs_ = sps->heightInMbs_;
        uint32_t totalMbs = static_cast<uint32_t>(widthInMbs_) * heightInMbs_;

        if (!dpbInitialized_)
        {
            dpb_.init(sps->width(), sps->height(), sps->numRefFrames_);
            dpbInitialized_ = true;
        }

        // Get a frame buffer from the DPB to decode into
        Frame* decodeTarget = dpb_.getDecodeTarget();
        if (!decodeTarget)
            return DecodeStatus::Error;

        // Also keep a reference in currentFrame_ for backwards compatibility
        if (!currentFrame_.isAllocated() ||
            currentFrame_.width() != sps->width() ||
            currentFrame_.height() != sps->height())
        {
            currentFrame_.allocate(sps->width(), sps->height());
        }

        // Resize context arrays
        nnzLuma_.resize(totalMbs * 16U, 0U);
        nnzCb_.resize(totalMbs * 4U, 0U);
        nnzCr_.resize(totalMbs * 4U, 0U);
        mbMotion_.resize(totalMbs);
        mbIntra4x4Modes_.resize(totalMbs * 16U, 2U); // Default DC(2)

        // Clear context for new frame
        if (isIdr)
        {
            dpb_.flush();
            // Re-get decode target after flush
            decodeTarget = dpb_.getDecodeTarget();
        }

        std::fill(nnzLuma_.begin(), nnzLuma_.end(), static_cast<uint8_t>(0U));
        std::fill(nnzCb_.begin(), nnzCb_.end(), static_cast<uint8_t>(0U));
        std::fill(nnzCr_.begin(), nnzCr_.end(), static_cast<uint8_t>(0U));
        std::fill(mbMotion_.begin(), mbMotion_.end(), MbMotionInfo{});
        std::fill(mbIntra4x4Modes_.begin(), mbIntra4x4Modes_.end(), static_cast<uint8_t>(2U));

#ifndef SUB0H264_NO_DEBUG_TRACE
        std::printf("[DBG] Slice header parsed: type=%u firstMb=%lu frameNum=%u qpDelta=%d bitOffset=%lu\n",
            static_cast<unsigned>(sh.sliceType_), (unsigned long)sh.firstMbInSlice_,
            sh.frameNum_, sh.sliceQpDelta_, (unsigned long)br.bitOffset());
#endif

        // Compute effective QP
        int32_t sliceQp = pps->picInitQp_ + sh.sliceQpDelta_;

        // Get reference frame for P-slices
        const Frame* refFrame = dpb_.getReference(0U);

        // Initialize CABAC engine if High profile
        bool useCabac = pps->isCabac();
        if (useCabac)
        {
            // Align to byte boundary after slice header
            br.alignToByte();

            // Initialize CABAC contexts
            uint32_t sliceTypeIdx = (sh.sliceType_ == SliceType::I) ? 2U :
                                    (sh.sliceType_ == SliceType::P) ? 0U : 1U;
            initCabacContexts(cabacCtx_.data(), sliceTypeIdx,
                              sh.cabacInitIdc_, sliceQp);

            // Initialize arithmetic engine
            cabacEngine_.init(br);

            // Resize skip tracking
            mbIsSkip_.resize(totalMbs, false);
            std::fill(mbIsSkip_.begin(), mbIsSkip_.end(), false);
        }

        // Decode macroblocks
        if (sh.sliceType_ == SliceType::I)
        {
            for (uint32_t mbAddr = sh.firstMbInSlice_; mbAddr < totalMbs; ++mbAddr)
            {
                uint32_t mbX = mbAddr % widthInMbs_;
                uint32_t mbY = mbAddr / widthInMbs_;

                if (useCabac)
                {
                    if (!decodeCabacIntraMb(br, *sps, *pps, sh, sliceQp, mbX, mbY))
                        break;
                }
                else
                {
                    if (!decodeIntraMb(br, *sps, *pps, sh, sliceQp, mbX, mbY))
                        break;
                }
            }
        }
        else if (sh.sliceType_ == SliceType::P)
        {
            if (!refFrame)
                return DecodeStatus::Error;

            if (useCabac)
            {
                // CABAC P-slice: per-MB skip flag instead of skip run
                for (uint32_t mbAddr = sh.firstMbInSlice_; mbAddr < totalMbs; ++mbAddr)
                {
                    uint32_t mbX = mbAddr % widthInMbs_;
                    uint32_t mbY = mbAddr / widthInMbs_;

                    // Decode mb_skip_flag
                    bool leftSkip = (mbX > 0U) && mbIsSkip_[mbY * widthInMbs_ + mbX - 1U];
                    bool topSkip = (mbY > 0U) && mbIsSkip_[(mbY - 1U) * widthInMbs_ + mbX];
                    uint32_t skipFlag = cabacDecodeMbSkipP(cabacEngine_, cabacCtx_.data(),
                                                           leftSkip, topSkip);

                    if (skipFlag)
                    {
                        mbIsSkip_[mbAddr] = true;
                        decodePSkipMb(*decodeTarget, *refFrame, mbX, mbY);
                    }
                    else
                    {
                        mbIsSkip_[mbAddr] = false;
                        uint32_t mbTypeRaw = cabacDecodeMbTypeP(cabacEngine_, cabacCtx_.data());

                        if (mbTypeRaw >= 5U)
                        {
                            mbTypeRaw -= 5U;
                            if (!decodeIntraMbInPSlice(br, *sps, *pps, sh, sliceQp,
                                                        mbTypeRaw, *decodeTarget, mbX, mbY))
                                break;
                        }
                        else
                        {
                            // CABAC inter MB: use CABAC for MVD + residual
                            decodeCabacPInterMb(br, *sps, *pps, sliceQp,
                                                mbTypeRaw, *decodeTarget, *refFrame, mbX, mbY);
                        }
                    }
                }
            }
            else
            {
                // CAVLC P-slice: skip run based
                uint32_t mbSkipRun = 0U;
                bool needSkipRun = true;

                for (uint32_t mbAddr = sh.firstMbInSlice_; mbAddr < totalMbs; ++mbAddr)
                {
                    uint32_t mbX = mbAddr % widthInMbs_;
                    uint32_t mbY = mbAddr / widthInMbs_;

                    if (needSkipRun)
                    {
                        mbSkipRun = br.readUev();
                        needSkipRun = false;
                    }

                    if (mbSkipRun > 0U)
                    {
                        decodePSkipMb(*decodeTarget, *refFrame, mbX, mbY);
                        --mbSkipRun;
                        if (mbSkipRun == 0U)
                            needSkipRun = true;
                    }
                    else
                    {
                        uint32_t mbTypeRaw = br.readUev();
                        needSkipRun = true;

                        if (mbTypeRaw >= 5U)
                        {
                            mbTypeRaw -= 5U;
                            if (!decodeIntraMbInPSlice(br, *sps, *pps, sh, sliceQp,
                                                        mbTypeRaw, *decodeTarget, mbX, mbY))
                                break;
                        }
                        else
                        {
                            decodePInterMb(br, *sps, *pps, sliceQp,
                                           mbTypeRaw, *decodeTarget, *refFrame, mbX, mbY);
                        }
                    }
                }
            }
        }

        // Deblocking filter pass (entire frame, after all MBs decoded)
        if (pps->deblockingFilterControlPresent_ == 0U ||
            sh.disableDeblockingFilter_ != 1U)
        {
            int32_t alphaOff = sh.sliceAlphaC0Offset_;
            int32_t betaOff = sh.sliceBetaOffset_;
            int32_t dbChromaQpIdx = clampQpIdx(sliceQp + pps->chromaQpIndexOffset_);
            int32_t dbChromaQp = cChromaQpTable[dbChromaQpIdx];

            Frame& dbFrame = (sh.sliceType_ == SliceType::I) ? currentFrame_ : *decodeTarget;

            for (uint32_t my = 0U; my < heightInMbs_; ++my)
            {
                for (uint32_t mx = 0U; mx < widthInMbs_; ++mx)
                {
                    bool mbIsIntra = (mbMotion_[my * widthInMbs_ + mx].refIdx == -1);
                    deblockMb(dbFrame, mx, my, sliceQp, dbChromaQp,
                              mbIsIntra, alphaOff, betaOff,
                              nnzLuma_.data(), mbMotion_.data(),
                              widthInMbs_, heightInMbs_);
                }
            }
        }

        // Sync frames: I-slice decodes into currentFrame_, P-slice into decodeTarget
        if (sh.sliceType_ == SliceType::I && decodeTarget->isAllocated())
        {
            // Copy currentFrame_ → decodeTarget so DPB has the I-frame
            std::memcpy(decodeTarget->yData(), currentFrame_.yData(),
                        currentFrame_.yStride() * currentFrame_.height());
            std::memcpy(decodeTarget->uData(), currentFrame_.uData(),
                        currentFrame_.uvStride() * (currentFrame_.height() / 2U));
            std::memcpy(decodeTarget->vData(), currentFrame_.vData(),
                        currentFrame_.uvStride() * (currentFrame_.height() / 2U));
        }
        else if (decodeTarget->isAllocated())
        {
            // Copy decodeTarget → currentFrame_ for public API access
            std::memcpy(currentFrame_.yData(), decodeTarget->yData(),
                        currentFrame_.yStride() * currentFrame_.height());
            std::memcpy(currentFrame_.uData(), decodeTarget->uData(),
                        currentFrame_.uvStride() * (currentFrame_.height() / 2U));
            std::memcpy(currentFrame_.vData(), decodeTarget->vData(),
                        currentFrame_.uvStride() * (currentFrame_.height() / 2U));
        }

        // Mark as reference for future P-frames
        if (nal.refIdc != 0U)
            dpb_.markAsReference(sh.frameNum_);

        ++frameCount_;
        return DecodeStatus::FrameDecoded;
    }

    /** Decode one intra macroblock. */
    bool decodeIntraMb(BitReader& br, const Sps& sps, const Pps& pps,
                       const SliceHeader& sh, int32_t sliceQp,
                       uint32_t mbX, uint32_t mbY) noexcept
    {
        if (!br.hasBits(1U))
            return false;

        // Read mb_type
        uint32_t mbTypeRaw = br.readUev();

        // Check for end-of-slice (bitstream exhausted)
        if (br.isExhausted())
            return false;

        int32_t currentQp = sliceQp;

#ifndef SUB0H264_NO_DEBUG_TRACE
        if (mbY == 0U && mbX < 12U)
            std::printf("[DBG] MB(%lu,0) mbType=%lu bitOff=%lu (start=%lu)\n",
                (unsigned long)mbX, (unsigned long)mbTypeRaw,
                (unsigned long)br.bitOffset(),
                (unsigned long)(br.bitOffset() - (mbTypeRaw < 4U ? 5U : 1U)));
        if (mbY == 0U && (mbX == 7U || mbX == 8U))
        {
            std::printf("[DBG]   MB(%lu,0) DETAIL: after mbType bitOff=%lu\n",
                (unsigned long)mbX, (unsigned long)br.bitOffset());
        }
#endif

        if (mbTypeRaw == 25U)
        {
            // I_PCM: raw samples, skip for now
            return true;
        }

        if (isI16x16(static_cast<uint8_t>(mbTypeRaw)))
        {
            // I_16x16 macroblock
            return decodeI16x16Mb(br, sps, pps, mbTypeRaw, currentQp, mbX, mbY);
        }
        else
        {
            // I_4x4 macroblock (mbType == 0)
            return decodeI4x4Mb(br, sps, pps, currentQp, mbX, mbY);
        }
    }

    /** Decode an I_16x16 macroblock. */
    bool decodeI16x16Mb(BitReader& br, const Sps& sps, const Pps& pps,
                         uint32_t mbTypeRaw, int32_t& qp,
                         uint32_t mbX, uint32_t mbY) noexcept
    {
        uint8_t predMode = i16x16PredMode(static_cast<uint8_t>(mbTypeRaw));
        uint8_t cbpLuma  = i16x16CbpLuma(static_cast<uint8_t>(mbTypeRaw));
        uint8_t cbpChroma = i16x16CbpChroma(static_cast<uint8_t>(mbTypeRaw));

        // Intra chroma prediction mode
#ifndef SUB0H264_NO_DEBUG_TRACE
        if (mbY == 0U && (mbX == 0U || mbX == 7U || mbX == 8U))
            std::printf("[DBG]   MB(%lu) Before chroma_pred: bitOff=%lu\n",
                (unsigned long)mbX, (unsigned long)br.bitOffset());
#endif
        uint32_t chromaPredMode = br.readUev();

#ifndef SUB0H264_NO_DEBUG_TRACE
        if (mbY == 0U && (mbX == 0U || mbX == 7U || mbX == 8U))
            std::printf("[DBG]   MB(%lu) After chroma_pred=%lu: bitOff=%lu\n",
                (unsigned long)mbX, (unsigned long)chromaPredMode, (unsigned long)br.bitOffset());
#endif

        // QP delta
        int32_t qpDelta = br.readSev();
#ifndef SUB0H264_NO_DEBUG_TRACE
        if (mbY == 0U && (mbX == 0U || mbX == 7U || mbX == 8U))
            std::printf("[DBG]   MB(%lu) After qpDelta=%d: bitOff=%lu\n",
                (unsigned long)mbX, qpDelta, (unsigned long)br.bitOffset());
#endif
        qp += qpDelta;
        if (qp < 0) qp += 52;
        if (qp > 51) qp -= 52;

        // 1. Generate 16x16 luma prediction
        uint8_t lumaPred[256];
        intraPred16x16(static_cast<Intra16x16Mode>(predMode),
                       currentFrame_, mbX, mbY, lumaPred);

#ifndef SUB0H264_NO_DEBUG_TRACE
        if (mbY == 0U && (mbX == 7U || mbX == 8U))
        {
            int32_t preNc = getLumaNc(mbX, mbY, 0U);
            uint32_t peek1 = br.peekBits(1U);
            uint32_t peek8 = br.peekBits(8U);
            std::printf("[DBG]   MB(%lu) DC block: bitOff=%lu nC=%d peek1=%u peek8=0x%02x\n",
                (unsigned long)mbX, (unsigned long)br.bitOffset(), preNc, peek1, peek8);
        }
#endif

        // 2. Decode luma DC block (4x4 Hadamard)
        int32_t dcNc = getLumaNc(mbX, mbY, 0U);
        ResidualBlock4x4 dcBlock;
        decodeResidualBlock4x4(br, dcNc, cMaxCoeff4x4, 0U, dcBlock);

        int16_t dcCoeffs[16];
        for (uint32_t i = 0U; i < 16U; ++i)
            dcCoeffs[i] = dcBlock.coeffs[i];

#ifndef SUB0H264_NO_DEBUG_TRACE
        // Temporary debug: trace DC decode for first MB
        if (mbX == 0U && mbY == 0U)
        {
            std::printf("[DBG] MB(0,0) I16x16: mbType=%lu pred=%u cbpL=%u cbpC=%u qp=%d bitOff=%lu\n",
                (unsigned long)mbTypeRaw, predMode, cbpLuma, cbpChroma, qp,
                (unsigned long)br.bitOffset());
            std::printf("[DBG]   DC block totalCoeff=%u nC=%d: ", dcBlock.totalCoeff, dcNc);
            for (uint32_t k = 0U; k < 16U; ++k) std::printf("%d ", dcBlock.coeffs[k]);
            std::printf("\n[DBG]   pred[0]=%u bitOff_after_dc=%lu\n", lumaPred[0],
                (unsigned long)br.bitOffset());
            // Peek at next few bits
            uint32_t peek = br.peekBits(16U);
            std::printf("[DBG]   next 16 bits: 0x%04x\n", peek);
        }
#endif

        // Inverse Hadamard
        inverseHadamard4x4(dcCoeffs);

        // Inverse quantize DC coefficients
        int32_t qpDiv6 = qp / 6;
        int32_t qpMod6 = qp % 6;
        int32_t dcScale = cDequantScale[qpMod6][0];
        for (uint32_t i = 0U; i < 16U; ++i)
        {
            if (dcCoeffs[i] != 0)
            {
                int32_t val = dcCoeffs[i] * dcScale;
                if (qpDiv6 >= 2)
                    val <<= (qpDiv6 - 2);
                else
                    val = (val + (1 << (1 - qpDiv6))) >> (2 - qpDiv6);
                dcCoeffs[i] = static_cast<int16_t>(val);
            }
        }

#ifndef SUB0H264_NO_DEBUG_TRACE
        if (mbX == 0U && mbY == 0U)
        {
            std::printf("[DBG]   After Hadamard+dequant DC: ");
            for (uint32_t k = 0U; k < 16U; ++k) std::printf("%d ", dcCoeffs[k]);
            std::printf("\n");
        }
#endif

        // 3. Decode and reconstruct each 4x4 luma sub-block
        uint8_t* mbLuma = currentFrame_.yMb(mbX, mbY);
        uint32_t yStride = currentFrame_.yStride();

        for (uint32_t blkIdx = 0U; blkIdx < 16U; ++blkIdx)
        {
            uint32_t blkX = (blkIdx & 3U) * 4U;
            uint32_t blkY = (blkIdx >> 2U) * 4U;

            int16_t coeffs[16] = {};
            coeffs[0] = dcCoeffs[blkIdx]; // DC from Hadamard

            if (cbpLuma)
            {
                // Decode AC coefficients (start from index 1)
                int32_t nc = getLumaNc(mbX, mbY, blkIdx);
                ResidualBlock4x4 acBlock;
                decodeResidualBlock4x4(br, nc, 15U, 1U, acBlock);

                // Merge AC into coeffs (skip DC at position 0)
                for (uint32_t i = 1U; i < 16U; ++i)
                    coeffs[i] = acBlock.coeffs[i];

                // Store NNZ for context
                uint32_t mbIdx = mbY * widthInMbs_ + mbX;
                nnzLuma_[mbIdx * 16U + blkIdx] = acBlock.totalCoeff;
            }

            // Inverse quantize AC coefficients only — DC was already dequantized
            // in the Hadamard path above. Dequanting position 0 again would
            // double-scale the DC coefficient (ITU-T H.264 §8.5.12.1).
            if (cbpLuma)
            {
                // Save DC, dequant all, restore DC (avoids conditional per-coeff)
                int16_t savedDc = coeffs[0];
                inverseQuantize4x4(coeffs, qp);
                coeffs[0] = savedDc;
            }

            // Inverse DCT + add prediction + clip
            uint8_t* predPtr = lumaPred + blkY * 16U + blkX;
            uint8_t* outPtr = mbLuma + blkY * yStride + blkX;
            inverseDct4x4AddPred(coeffs, predPtr, 16U, outPtr, yStride);
        }

        // 4. Decode chroma (simplified — DC prediction + optional residual)
        decodeChromaMb(br, pps, chromaPredMode, cbpChroma, qp, mbX, mbY);

        return true;
    }

    /** Decode an I_4x4 macroblock. */
    /** Get intra 4x4 prediction mode for a neighboring block.
     *  Returns DC(2) if neighbor is unavailable or not intra-4x4.
     *  For cross-MB lookups, uses mbIntra4x4Modes_ which stores
     *  all previously decoded MB intra modes.
     *
     *  Block layout within MB (raster scan):
     *    0  1  2  3
     *    4  5  6  7
     *    8  9  10 11
     *    12 13 14 15
     *
     *  Reference: ITU-T H.264 §8.3.1.1
     */
    uint8_t getNeighborIntra4x4Mode(uint32_t mbX, uint32_t mbY,
                                     uint32_t blkIdx, bool isLeft,
                                     const uint8_t* curMbModes) const noexcept
    {
        /// Default intra 4x4 prediction mode when neighbor unavailable — ITU-T H.264 §8.3.1.1.
        static constexpr uint8_t cDefaultIntra4x4Mode = 2U; // DC

        uint32_t blkX = blkIdx & 3U;
        uint32_t blkY = blkIdx >> 2U;

        if (isLeft)
        {
            if (blkX > 0U)
                return curMbModes[blkIdx - 1U]; // Within same MB
            if (mbX == 0U)
                return cDefaultIntra4x4Mode; // No left MB
            // Left MB's rightmost column: same row, column 3
            uint32_t leftMbIdx = mbY * widthInMbs_ + (mbX - 1U);
            uint32_t leftBlk = blkY * 4U + 3U;
            return mbIntra4x4Modes_[leftMbIdx * 16U + leftBlk];
        }
        else // top
        {
            if (blkY > 0U)
                return curMbModes[blkIdx - 4U]; // Within same MB
            if (mbY == 0U)
                return cDefaultIntra4x4Mode; // No top MB
            // Top MB's bottom row: same column, row 3
            uint32_t topMbIdx = (mbY - 1U) * widthInMbs_ + mbX;
            uint32_t topBlk = 12U + blkX;
            return mbIntra4x4Modes_[topMbIdx * 16U + topBlk];
        }
    }

    bool decodeI4x4Mb(BitReader& br, const Sps& sps, const Pps& pps,
                       int32_t& qp, uint32_t mbX, uint32_t mbY) noexcept
    {
        // Read intra 4x4 prediction modes for all 16 blocks.
        // ITU-T H.264 §8.3.1.1: most probable mode = min(leftMode, topMode).
        // If prev_intra4x4_pred_mode_flag=1: use MPM.
        // If flag=0: read rem_intra4x4_pred_mode (3 bits).
        //   If rem < MPM: mode = rem. Else: mode = rem + 1.
        uint8_t predModes[16] = {};

#ifndef SUB0H264_NO_DEBUG_TRACE
        uint32_t predModeStartBit = 0U;
        if (mbX == 10U && mbY == 0U)
        {
            predModeStartBit = br.bitOffset();
            std::printf("[DBG] MB(10,0) pred modes start at bitOff=%lu\n",
                (unsigned long)predModeStartBit);
        }
#endif

        for (uint32_t i = 0U; i < 16U; ++i)
        {
            uint8_t leftMode = getNeighborIntra4x4Mode(mbX, mbY, i, true, predModes);
            uint8_t topMode  = getNeighborIntra4x4Mode(mbX, mbY, i, false, predModes);
            uint8_t mpm = (leftMode < topMode) ? leftMode : topMode;

            uint32_t prevFlag = br.readBit();
            if (prevFlag)
            {
                predModes[i] = mpm;
            }
            else
            {
                uint8_t rem = static_cast<uint8_t>(br.readBits(3U));
                predModes[i] = (rem < mpm) ? rem : static_cast<uint8_t>(rem + 1U);
            }

#ifndef SUB0H264_NO_DEBUG_TRACE
            if (mbX == 10U && mbY == 0U && i < 4U)
                std::printf("[DBG]   blk%lu: prevFlag=%lu mpm=%u mode=%u\n",
                    (unsigned long)i, (unsigned long)prevFlag,
                    mpm, predModes[i]);
#endif
        }

        // Intra chroma prediction mode
        uint32_t chromaPredMode = br.readUev();

        // Coded block pattern
        uint32_t cbpCode = br.readUev();
        uint8_t cbp = 0U;
        if (cbpCode < 48U)
            cbp = cCbpTable[cbpCode][0]; // Intra
        uint8_t cbpLuma = cbp & 0x0FU;
        uint8_t cbpChroma = (cbp >> 4U) & 0x03U;

#ifndef SUB0H264_NO_DEBUG_TRACE
        if (mbX == 10U && mbY == 0U)
            std::printf("[DBG] MB(10,0) cbpCode=%lu → cbp=0x%02x bitOff=%lu\n",
                (unsigned long)cbpCode, cbp, (unsigned long)br.bitOffset());
#endif

        // QP delta (only if CBP > 0)
        if (cbp > 0U)
        {
            int32_t qpDelta = br.readSev();
            qp += qpDelta;
            if (qp < 0) qp += 52;
            if (qp > 51) qp -= 52;
        }

#ifndef SUB0H264_NO_DEBUG_TRACE
        if (mbX == 10U && mbY == 0U)
        {
            std::printf("[DBG] MB(10,0) I4x4: modes=[");
            for (uint32_t k = 0U; k < 16U; ++k) std::printf("%u ", predModes[k]);
            std::printf("] cbp=0x%02x cbpL=%u cbpC=%u qp=%d\n", cbp, cbpLuma, cbpChroma, qp);
        }
#endif

        // Decode and reconstruct each 4x4 luma block
        uint8_t* mbLuma = currentFrame_.yMb(mbX, mbY);
        uint32_t yStride = currentFrame_.yStride();

        for (uint32_t blkIdx = 0U; blkIdx < 16U; ++blkIdx)
        {
            uint32_t blkX = (blkIdx & 3U) * 4U;
            uint32_t blkY = (blkIdx >> 2U) * 4U;

            // Generate 4x4 prediction
            uint8_t pred4x4[16];

            // Get neighbor samples for this 4x4 block
            uint32_t absX = mbX * cMbSize + blkX;
            uint32_t absY = mbY * cMbSize + blkY;

            uint8_t topBuf[8] = {};
            uint8_t leftBuf[4] = {};
            uint8_t topLeftVal = cDefaultPredValue;
            const uint8_t* top = nullptr;
            const uint8_t* topRight = nullptr;
            const uint8_t* left = nullptr;
            const uint8_t* topLeft = nullptr;

            if (absY > 0U)
            {
                const uint8_t* row = currentFrame_.yRow(absY - 1U);
                for (uint32_t i = 0U; i < 4U; ++i) topBuf[i] = row[absX + i];
                top = topBuf;

                if (absX + 4U < currentFrame_.width())
                {
                    for (uint32_t i = 0U; i < 4U; ++i) topBuf[4 + i] = row[absX + 4U + i];
                    topRight = topBuf + 4U;
                }
            }
            if (absX > 0U)
            {
                for (uint32_t i = 0U; i < 4U; ++i)
                    leftBuf[i] = currentFrame_.y(absX - 1U, absY + i);
                left = leftBuf;
            }
            if (absX > 0U && absY > 0U)
            {
                topLeftVal = currentFrame_.y(absX - 1U, absY - 1U);
                topLeft = &topLeftVal;
            }

            intraPred4x4(static_cast<Intra4x4Mode>(predModes[blkIdx]),
                         top, topRight, left, topLeft, pred4x4);

            // Decode residual if CBP indicates this 8x8 group has coefficients
            uint32_t group8x8 = (blkY >= 8U ? 2U : 0U) + (blkX >= 8U ? 1U : 0U);
            bool hasResidual = (cbpLuma >> group8x8) & 1U;

            int16_t coeffs[16] = {};
            if (hasResidual)
            {
                int32_t nc = getLumaNc(mbX, mbY, blkIdx);
                ResidualBlock4x4 resBlock;
                decodeResidualBlock4x4(br, nc, cMaxCoeff4x4, 0U, resBlock);

                for (uint32_t i = 0U; i < 16U; ++i)
                    coeffs[i] = resBlock.coeffs[i];

                uint32_t mbIdx = mbY * widthInMbs_ + mbX;
                nnzLuma_[mbIdx * 16U + blkIdx] = resBlock.totalCoeff;

                inverseQuantize4x4(coeffs, qp);
            }

#ifndef SUB0H264_NO_DEBUG_TRACE
            if (mbX == 10U && mbY == 0U && blkIdx == 0U)
            {
                std::printf("[DBG]   blk0: mode=%u pred=[%u %u %u %u] hasRes=%d\n",
                    predModes[0], pred4x4[0], pred4x4[1], pred4x4[2], pred4x4[3], hasResidual);
                if (hasResidual)
                {
                    std::printf("[DBG]   coeffs (dequant)=[");
                    for (uint32_t k = 0U; k < 16U; ++k) std::printf("%d ", coeffs[k]);
                    std::printf("]\n");
                }
            }
#endif

            // Reconstruct: inverse DCT + prediction + clip
            uint8_t* outPtr = mbLuma + blkY * yStride + blkX;
            inverseDct4x4AddPred(coeffs, pred4x4, 4U, outPtr, yStride);
        }

        // Store intra 4x4 modes for cross-MB MPM derivation
        {
            uint32_t mbIdx = mbY * widthInMbs_ + mbX;
            for (uint32_t k = 0U; k < 16U; ++k)
                mbIntra4x4Modes_[mbIdx * 16U + k] = predModes[k];
        }

        // Decode chroma
        decodeChromaMb(br, pps, chromaPredMode, cbpChroma, qp, mbX, mbY);

        return true;
    }

    /** Decode chroma for one macroblock. */
    void decodeChromaMb(BitReader& br, const Pps& pps,
                        uint32_t chromaPredMode, uint8_t cbpChroma,
                        int32_t qp, uint32_t mbX, uint32_t mbY) noexcept
    {
#ifndef SUB0H264_NO_DEBUG_TRACE
        if (mbY == 0U && mbX < 2U)
            std::printf("[DBG] MB(%lu,0) chromaMb START bitOff=%lu cbpC=%u\n",
                (unsigned long)mbX, (unsigned long)br.bitOffset(), cbpChroma);
#endif
        auto chromaMode = static_cast<IntraChromaMode>(chromaPredMode);

        // Generate chroma predictions (8x8 for each plane)
        uint8_t predU[64], predV[64];
        intraPredChroma8x8(chromaMode, currentFrame_, mbX, mbY, true, predU);
        intraPredChroma8x8(chromaMode, currentFrame_, mbX, mbY, false, predV);

        // Chroma QP
        int32_t chromaQpIdx = qp + pps.chromaQpIndexOffset_;
        chromaQpIdx = clampQpIdx(chromaQpIdx);
        int32_t chromaQp = cChromaQpTable[chromaQpIdx];

        // Decode chroma DC if cbpChroma >= 1
        int16_t dcCb[4] = {}, dcCr[4] = {};
        if (cbpChroma >= 1U)
        {
            ResidualBlock4x4 dcBlockCb, dcBlockCr;
            decodeResidualBlock4x4(br, -1, 4U, 0U, dcBlockCb);
            decodeResidualBlock4x4(br, -1, 4U, 0U, dcBlockCr);

            for (uint32_t i = 0U; i < 4U; ++i)
            {
                dcCb[i] = dcBlockCb.coeffs[i];
                dcCr[i] = dcBlockCr.coeffs[i];
            }

            inverseHadamard2x2(dcCb);
            inverseHadamard2x2(dcCr);

            // Dequantize chroma DC
            int32_t cqpDiv6 = chromaQp / 6;
            int32_t cqpMod6 = chromaQp % 6;
            int32_t cScale = cDequantScale[cqpMod6][0];
            for (uint32_t i = 0U; i < 4U; ++i)
            {
                if (dcCb[i] != 0)
                {
                    int32_t val = dcCb[i] * cScale;
                    if (cqpDiv6 >= 1)
                        val <<= (cqpDiv6 - 1);
                    else
                        val = (val + 1) >> 1;
                    dcCb[i] = static_cast<int16_t>(val);
                }
                if (dcCr[i] != 0)
                {
                    int32_t val = dcCr[i] * cScale;
                    if (cqpDiv6 >= 1)
                        val <<= (cqpDiv6 - 1);
                    else
                        val = (val + 1) >> 1;
                    dcCr[i] = static_cast<int16_t>(val);
                }
            }
        }

        // Reconstruct chroma 4x4 blocks
        uint8_t* mbU = currentFrame_.uMb(mbX, mbY);
        uint8_t* mbV = currentFrame_.vMb(mbX, mbY);
        uint32_t uvStride = currentFrame_.uvStride();

        for (uint32_t blkIdx = 0U; blkIdx < 4U; ++blkIdx)
        {
            uint32_t blkX = (blkIdx & 1U) * 4U;
            uint32_t blkY = (blkIdx >> 1U) * 4U;

            // Cb block
            int16_t cbCoeffs[16] = {};
            cbCoeffs[0] = dcCb[blkIdx];

            if (cbpChroma >= 2U)
            {
                ResidualBlock4x4 acBlock;
                decodeResidualBlock4x4(br, 0, 15U, 1U, acBlock);
                for (uint32_t i = 1U; i < 16U; ++i)
                    cbCoeffs[i] = acBlock.coeffs[i];
                inverseQuantize4x4(cbCoeffs, chromaQp);
            }

            uint8_t* predCbPtr = predU + blkY * 8U + blkX;
            uint8_t* outCbPtr = mbU + blkY * uvStride + blkX;
            inverseDct4x4AddPred(cbCoeffs, predCbPtr, 8U, outCbPtr, uvStride);

            // Cr block
            int16_t crCoeffs[16] = {};
            crCoeffs[0] = dcCr[blkIdx];

            if (cbpChroma >= 2U)
            {
                ResidualBlock4x4 acBlock;
                decodeResidualBlock4x4(br, 0, 15U, 1U, acBlock);
                for (uint32_t i = 1U; i < 16U; ++i)
                    crCoeffs[i] = acBlock.coeffs[i];
                inverseQuantize4x4(crCoeffs, chromaQp);
            }

            uint8_t* predCrPtr = predV + blkY * 8U + blkX;
            uint8_t* outCrPtr = mbV + blkY * uvStride + blkX;
            inverseDct4x4AddPred(crCoeffs, predCrPtr, 8U, outCrPtr, uvStride);
        }
    }

    // ── P-frame decode methods ──────────────────────────────────────────

    /** Get MV neighbor info for a macroblock. */
    MbMotionInfo getMbMotionNeighbor(uint32_t mbX, uint32_t mbY, int32_t dx, int32_t dy) const noexcept
    {
        int32_t nx = static_cast<int32_t>(mbX) + dx;
        int32_t ny = static_cast<int32_t>(mbY) + dy;
        if (nx < 0 || ny < 0 || nx >= widthInMbs_ || ny >= heightInMbs_)
            return {};
        return mbMotion_[ny * widthInMbs_ + nx];
    }

    /** Decode a P_Skip macroblock: inferred MV, no residual. */
    void decodePSkipMb(Frame& target, const Frame& ref,
                        uint32_t mbX, uint32_t mbY) noexcept
    {
        // Infer MV from spatial neighbors (ref_idx = 0)
        MbMotionInfo left    = getMbMotionNeighbor(mbX, mbY, -1, 0);
        MbMotionInfo top     = getMbMotionNeighbor(mbX, mbY, 0, -1);
        MbMotionInfo topRight = getMbMotionNeighbor(mbX, mbY, 1, -1);
        if (!topRight.available)
            topRight = getMbMotionNeighbor(mbX, mbY, -1, -1); // Use top-left

        // Special skip MV derivation: if left or top is intra or has MV=(0,0),ref=0
        // then predicted MV is (0,0). ITU-T H.264 §8.4.1.1
        MotionVector skipMv = {0, 0};
        if (left.available && top.available &&
            !(left.refIdx == 0 && left.mv.x == 0 && left.mv.y == 0) &&
            !(top.refIdx == 0 && top.mv.x == 0 && top.mv.y == 0))
        {
            skipMv = computeMvPredictor(left, top, topRight, 0);
        }

        // Store MV for this MB
        uint32_t mbIdx = mbY * widthInMbs_ + mbX;
        mbMotion_[mbIdx] = { skipMv, 0, true };

        // Motion compensation: copy 16x16 block from reference
        int32_t refX = static_cast<int32_t>(mbX * cMbSize) + (skipMv.x >> 2);
        int32_t refY = static_cast<int32_t>(mbY * cMbSize) + (skipMv.y >> 2);
        uint32_t dx = static_cast<uint32_t>(skipMv.x) & 3U;
        uint32_t dy = static_cast<uint32_t>(skipMv.y) & 3U;

        // Luma
        lumaMotionComp(ref, refX, refY, dx, dy, cMbSize, cMbSize,
                        target.yMb(mbX, mbY), target.yStride());

        // Chroma — derive from luma MV (divide by 2, eighth-pel)
        int32_t chromaRefX = static_cast<int32_t>(mbX * cChromaBlockSize) + (skipMv.x >> 3);
        int32_t chromaRefY = static_cast<int32_t>(mbY * cChromaBlockSize) + (skipMv.y >> 3);
        uint32_t cdx = static_cast<uint32_t>(skipMv.x) & 7U;
        uint32_t cdy = static_cast<uint32_t>(skipMv.y) & 7U;

        chromaMotionComp(ref, chromaRefX, chromaRefY, cdx, cdy,
                         cChromaBlockSize, cChromaBlockSize, true,
                         target.uMb(mbX, mbY), target.uvStride());
        chromaMotionComp(ref, chromaRefX, chromaRefY, cdx, cdy,
                         cChromaBlockSize, cChromaBlockSize, false,
                         target.vMb(mbX, mbY), target.uvStride());

        // NNZ = 0 for skip MBs
        std::fill_n(&nnzLuma_[mbIdx * 16U], 16U, static_cast<uint8_t>(0U));
    }

    /** Decode a P-inter macroblock (16x16 only for now). */
    void decodePInterMb(BitReader& br, const Sps& sps, const Pps& pps,
                         int32_t sliceQp, uint32_t mbTypeRaw,
                         Frame& target, const Frame& ref,
                         uint32_t mbX, uint32_t mbY) noexcept
    {
        // For now: only P_L0_16x16 (mbTypeRaw == 0)
        // mbTypeRaw: 0=16x16, 1=16x8, 2=8x16, 3=8x8, 4=8x8ref0

        // Read ref_idx_l0 (only if num_ref_frames > 1, simplified: skip for 1 ref)
        // For baseline with single ref, ref_idx is always 0

        // Read MVD
        int16_t mvdX = static_cast<int16_t>(br.readSev());
        int16_t mvdY = static_cast<int16_t>(br.readSev());

        // Compute MV predictor
        MbMotionInfo left    = getMbMotionNeighbor(mbX, mbY, -1, 0);
        MbMotionInfo top     = getMbMotionNeighbor(mbX, mbY, 0, -1);
        MbMotionInfo topRight = getMbMotionNeighbor(mbX, mbY, 1, -1);
        if (!topRight.available)
            topRight = getMbMotionNeighbor(mbX, mbY, -1, -1);

        MotionVector mvp = computeMvPredictor(left, top, topRight, 0);
        MotionVector mv = { static_cast<int16_t>(mvp.x + mvdX),
                            static_cast<int16_t>(mvp.y + mvdY) };

        // Store MV
        uint32_t mbIdx = mbY * widthInMbs_ + mbX;
        mbMotion_[mbIdx] = { mv, 0, true };

        // Motion compensation: luma
        int32_t refX = static_cast<int32_t>(mbX * cMbSize) + (mv.x >> 2);
        int32_t refY = static_cast<int32_t>(mbY * cMbSize) + (mv.y >> 2);
        uint32_t dx = static_cast<uint32_t>(mv.x) & 3U;
        uint32_t dy = static_cast<uint32_t>(mv.y) & 3U;

        uint8_t predLuma[256];
        lumaMotionComp(ref, refX, refY, dx, dy, cMbSize, cMbSize, predLuma, cMbSize);

        // Chroma motion comp
        int32_t chromaRefX = static_cast<int32_t>(mbX * cChromaBlockSize) + (mv.x >> 3);
        int32_t chromaRefY = static_cast<int32_t>(mbY * cChromaBlockSize) + (mv.y >> 3);
        uint32_t cdx = static_cast<uint32_t>(mv.x) & 7U;
        uint32_t cdy = static_cast<uint32_t>(mv.y) & 7U;

        uint8_t predU[64], predV[64];
        chromaMotionComp(ref, chromaRefX, chromaRefY, cdx, cdy,
                         cChromaBlockSize, cChromaBlockSize, true, predU, cChromaBlockSize);
        chromaMotionComp(ref, chromaRefX, chromaRefY, cdx, cdy,
                         cChromaBlockSize, cChromaBlockSize, false, predV, cChromaBlockSize);

        // Read CBP
        uint32_t cbpCode = br.readUev();
        uint8_t cbp = 0U;
        if (cbpCode < 48U)
            cbp = cCbpTable[cbpCode][1]; // Inter CBP mapping
        uint8_t cbpLuma = cbp & 0x0FU;
        uint8_t cbpChroma = (cbp >> 4U) & 0x03U;

        int32_t qp = sliceQp;
        if (cbp > 0U)
        {
            int32_t qpDelta = br.readSev();
            qp += qpDelta;
            if (qp < 0) qp += 52;
            if (qp > 51) qp -= 52;
        }

        // Decode luma residual and reconstruct
        uint32_t yStride = target.yStride();
        for (uint32_t blkIdx = 0U; blkIdx < 16U; ++blkIdx)
        {
            uint32_t blkX = (blkIdx & 3U) * 4U;
            uint32_t blkY = (blkIdx >> 2U) * 4U;

            uint32_t group8x8 = (blkY >= 8U ? 2U : 0U) + (blkX >= 8U ? 1U : 0U);
            bool hasResidual = (cbpLuma >> group8x8) & 1U;

            int16_t coeffs[16] = {};
            if (hasResidual)
            {
                int32_t nc = getLumaNc(mbX, mbY, blkIdx);
                ResidualBlock4x4 resBlock;
                decodeResidualBlock4x4(br, nc, cMaxCoeff4x4, 0U, resBlock);
                for (uint32_t i = 0U; i < 16U; ++i)
                    coeffs[i] = resBlock.coeffs[i];
                nnzLuma_[mbIdx * 16U + blkIdx] = resBlock.totalCoeff;
                inverseQuantize4x4(coeffs, qp);
            }

            uint8_t* predPtr = predLuma + blkY * cMbSize + blkX;
            uint8_t* outPtr = target.yMb(mbX, mbY) + blkY * yStride + blkX;
            inverseDct4x4AddPred(coeffs, predPtr, cMbSize, outPtr, yStride);
        }

        // Decode chroma residual and reconstruct
        int32_t chromaQpIdx = clampQpIdx(qp + pps.chromaQpIndexOffset_);
        int32_t chromaQp = cChromaQpTable[chromaQpIdx];

        int16_t dcCb[4] = {}, dcCr[4] = {};
        if (cbpChroma >= 1U)
        {
            ResidualBlock4x4 dcBlockCb, dcBlockCr;
            decodeResidualBlock4x4(br, -1, 4U, 0U, dcBlockCb);
            decodeResidualBlock4x4(br, -1, 4U, 0U, dcBlockCr);
            for (uint32_t i = 0U; i < 4U; ++i) { dcCb[i] = dcBlockCb.coeffs[i]; dcCr[i] = dcBlockCr.coeffs[i]; }
            inverseHadamard2x2(dcCb);
            inverseHadamard2x2(dcCr);

            int32_t cqpDiv6 = chromaQp / 6;
            int32_t cqpMod6 = chromaQp % 6;
            int32_t cScale = cDequantScale[cqpMod6][0];
            for (uint32_t i = 0U; i < 4U; ++i)
            {
                if (dcCb[i] != 0) { int32_t v = dcCb[i] * cScale; dcCb[i] = static_cast<int16_t>(cqpDiv6 >= 1 ? v << (cqpDiv6 - 1) : (v + 1) >> 1); }
                if (dcCr[i] != 0) { int32_t v = dcCr[i] * cScale; dcCr[i] = static_cast<int16_t>(cqpDiv6 >= 1 ? v << (cqpDiv6 - 1) : (v + 1) >> 1); }
            }
        }

        uint32_t uvStride = target.uvStride();
        for (uint32_t blkIdx = 0U; blkIdx < 4U; ++blkIdx)
        {
            uint32_t blkX = (blkIdx & 1U) * 4U;
            uint32_t blkY = (blkIdx >> 1U) * 4U;

            int16_t cbCoeffs[16] = {}, crCoeffs[16] = {};
            cbCoeffs[0] = dcCb[blkIdx];
            crCoeffs[0] = dcCr[blkIdx];

            if (cbpChroma >= 2U)
            {
                ResidualBlock4x4 acCb, acCr;
                decodeResidualBlock4x4(br, 0, 15U, 1U, acCb);
                for (uint32_t i = 1U; i < 16U; ++i) cbCoeffs[i] = acCb.coeffs[i];
                inverseQuantize4x4(cbCoeffs, chromaQp);
                decodeResidualBlock4x4(br, 0, 15U, 1U, acCr);
                for (uint32_t i = 1U; i < 16U; ++i) crCoeffs[i] = acCr.coeffs[i];
                inverseQuantize4x4(crCoeffs, chromaQp);
            }

            inverseDct4x4AddPred(cbCoeffs, predU + blkY * 8U + blkX, 8U, target.uMb(mbX, mbY) + blkY * uvStride + blkX, uvStride);
            inverseDct4x4AddPred(crCoeffs, predV + blkY * 8U + blkX, 8U, target.vMb(mbX, mbY) + blkY * uvStride + blkX, uvStride);
        }
    }

    /** Decode an intra MB within a P-slice (mb_type offset already applied). */
    bool decodeIntraMbInPSlice(BitReader& br, const Sps& sps, const Pps& pps,
                                const SliceHeader& sh, int32_t sliceQp,
                                uint32_t mbTypeRaw, Frame& target,
                                uint32_t mbX, uint32_t mbY) noexcept
    {
        uint32_t mbIdx = mbY * widthInMbs_ + mbX;
        mbMotion_[mbIdx] = { {0, 0}, -1, true };

        int32_t qp = sliceQp;
        if (mbTypeRaw == 25U) return true;

        if (isI16x16(static_cast<uint8_t>(mbTypeRaw)))
            return decodeI16x16Mb(br, sps, pps, mbTypeRaw, qp, mbX, mbY);
        else
            return decodeI4x4Mb(br, sps, pps, qp, mbX, mbY);
    }

    // ── CABAC-specific MB decode methods ────────────────────────────────

    /** Decode an intra MB using CABAC. */
    bool decodeCabacIntraMb(BitReader& br, const Sps& sps, const Pps& pps,
                             const SliceHeader& sh, int32_t sliceQp,
                             uint32_t mbX, uint32_t mbY) noexcept
    {
        (void)br; (void)sh;
        uint32_t mbIdx = mbY * widthInMbs_ + mbX;
        mbMotion_[mbIdx] = { {0, 0}, -1, true };

        bool leftIsIntra = (mbX == 0U) || (mbMotion_[mbIdx - 1U].refIdx == -1);
        bool topIsIntra = (mbY == 0U) || (mbMotion_[mbIdx - widthInMbs_].refIdx == -1);

        uint32_t mbTypeRaw = cabacDecodeMbTypeI(cabacEngine_, cabacCtx_.data(),
                                                 leftIsIntra, topIsIntra);

        if (mbTypeRaw == 25U) return true; // I_PCM
        if (mbTypeRaw == 0U)
        {
            // I_4x4: decode pred modes + residual via CABAC
            return decodeCabacI4x4Mb(sps, pps, sliceQp, mbX, mbY);
        }
        else
        {
            // I_16x16: decode using existing path with CABAC residual
            return decodeCabacI16x16Mb(sps, pps, mbTypeRaw, sliceQp, mbX, mbY);
        }
    }

    /** Decode I_4x4 MB with CABAC residual. */
    bool decodeCabacI4x4Mb(const Sps& sps, const Pps& pps,
                            int32_t& qp, uint32_t mbX, uint32_t mbY) noexcept
    {
        (void)sps;
        // Read intra 4x4 prediction modes using CABAC
        uint8_t predModes[16] = {};
        for (uint32_t i = 0U; i < 16U; ++i)
        {
            uint8_t result = cabacDecodeIntra4x4PredMode(cabacEngine_, cabacCtx_.data());
            if (result == 0xFFU)
                predModes[i] = 2U; // Most probable → DC (simplified)
            else
                predModes[i] = result;
        }

        // Chroma pred mode
        uint32_t mbIdx = mbY * widthInMbs_ + mbX;
        bool leftIntra = (mbX == 0U) || (mbMotion_[mbIdx - 1U].refIdx == -1);
        bool topIntra = (mbY == 0U) || (mbMotion_[mbIdx - widthInMbs_].refIdx == -1);
        uint32_t chromaPredMode = cabacDecodeIntraChromaMode(cabacEngine_, cabacCtx_.data(),
                                                              leftIntra, topIntra);

        // CBP via CABAC
        uint8_t cbp = cabacDecodeCbp(cabacEngine_, cabacCtx_.data(),
                                      true, true, false, false);
        uint8_t cbpLuma = cbp & 0x0FU;
        uint8_t cbpChroma = (cbp >> 4U) & 0x03U;

        // QP delta
        if (cbp > 0U)
        {
            int32_t qpDelta = cabacDecodeMbQpDelta(cabacEngine_, cabacCtx_.data(), false);
            qp += qpDelta;
            if (qp < 0) qp += 52;
            if (qp > 51) qp -= 52;
        }

        // Decode luma 4x4 blocks
        uint8_t* mbLuma = currentFrame_.yMb(mbX, mbY);
        uint32_t yStride = currentFrame_.yStride();

        for (uint32_t blkIdx = 0U; blkIdx < 16U; ++blkIdx)
        {
            uint32_t blkX = (blkIdx & 3U) * 4U;
            uint32_t blkY = (blkIdx >> 2U) * 4U;
            uint32_t absX = mbX * cMbSize + blkX;
            uint32_t absY = mbY * cMbSize + blkY;

            // Prediction
            uint8_t pred4x4[16];
            uint8_t topBuf[8] = {}, leftBuf[4] = {}, topLeftVal = cDefaultPredValue;
            const uint8_t *top = nullptr, *topRight = nullptr, *left = nullptr, *topLeft = nullptr;
            if (absY > 0U) { const uint8_t* row = currentFrame_.yRow(absY - 1U); for (uint32_t i = 0U; i < 4U; ++i) topBuf[i] = row[absX + i]; top = topBuf; if (absX + 4U < currentFrame_.width()) { for (uint32_t i = 0U; i < 4U; ++i) topBuf[4+i] = row[absX+4U+i]; topRight = topBuf+4U; } }
            if (absX > 0U) { for (uint32_t i = 0U; i < 4U; ++i) leftBuf[i] = currentFrame_.y(absX-1U, absY+i); left = leftBuf; }
            if (absX > 0U && absY > 0U) { topLeftVal = currentFrame_.y(absX-1U, absY-1U); topLeft = &topLeftVal; }

            intraPred4x4(static_cast<Intra4x4Mode>(predModes[blkIdx]), top, topRight, left, topLeft, pred4x4);

            // Residual via CABAC
            uint32_t group8x8 = (blkY >= 8U ? 2U : 0U) + (blkX >= 8U ? 1U : 0U);
            bool hasResidual = (cbpLuma >> group8x8) & 1U;

            int16_t coeffs[16] = {};
            if (hasResidual)
            {
                cabacDecodeResidual4x4(cabacEngine_, cabacCtx_.data(), coeffs, 16U, 2U);
                nnzLuma_[mbIdx * 16U + blkIdx] = 1U; // Simplified NNZ tracking
                inverseQuantize4x4(coeffs, qp);
            }

            uint8_t* outPtr = mbLuma + blkY * yStride + blkX;
            inverseDct4x4AddPred(coeffs, pred4x4, 4U, outPtr, yStride);
        }

        // Chroma (use existing CAVLC chroma path — structure is the same,
        // only entropy coding differs. For CABAC chroma, use CABAC residual.)
        decodeChromaCabac(pps, chromaPredMode, cbpChroma, qp, mbX, mbY);
        return true;
    }

    /** Decode I_16x16 MB with CABAC. */
    bool decodeCabacI16x16Mb(const Sps& sps, const Pps& pps,
                              uint32_t mbTypeRaw, int32_t& qp,
                              uint32_t mbX, uint32_t mbY) noexcept
    {
        (void)sps;
        uint8_t predMode = i16x16PredMode(static_cast<uint8_t>(mbTypeRaw));
        uint8_t cbpLuma  = i16x16CbpLuma(static_cast<uint8_t>(mbTypeRaw));
        uint8_t cbpChroma = i16x16CbpChroma(static_cast<uint8_t>(mbTypeRaw));

        uint32_t mbIdx = mbY * widthInMbs_ + mbX;
        bool leftIntra = (mbX == 0U) || (mbMotion_[mbIdx - 1U].refIdx == -1);
        bool topIntra = (mbY == 0U) || (mbMotion_[mbIdx - widthInMbs_].refIdx == -1);
        uint32_t chromaPredMode = cabacDecodeIntraChromaMode(cabacEngine_, cabacCtx_.data(),
                                                              leftIntra, topIntra);

        // QP delta
        int32_t qpDelta = cabacDecodeMbQpDelta(cabacEngine_, cabacCtx_.data(), false);
        qp += qpDelta;
        if (qp < 0) qp += 52;
        if (qp > 51) qp -= 52;

        // Generate 16x16 prediction
        uint8_t lumaPred[256];
        intraPred16x16(static_cast<Intra16x16Mode>(predMode), currentFrame_, mbX, mbY, lumaPred);

        // Decode DC block via CABAC (category 0 = Luma DC)
        int16_t dcCoeffs[16] = {};
        cabacDecodeResidual4x4(cabacEngine_, cabacCtx_.data(), dcCoeffs, 16U, 0U);
        inverseHadamard4x4(dcCoeffs);

        int32_t qpDiv6 = qp / 6;
        int32_t qpMod6 = qp % 6;
        int32_t dcScale = cDequantScale[qpMod6][0];
        for (uint32_t i = 0U; i < 16U; ++i)
        {
            if (dcCoeffs[i] != 0)
            {
                int32_t val = dcCoeffs[i] * dcScale;
                dcCoeffs[i] = static_cast<int16_t>(qpDiv6 >= 2 ? val << (qpDiv6 - 2) : (val + (1 << (1 - qpDiv6))) >> (2 - qpDiv6));
            }
        }

        // Decode AC blocks
        uint8_t* mbLuma = currentFrame_.yMb(mbX, mbY);
        uint32_t yStride = currentFrame_.yStride();

        for (uint32_t blkIdx = 0U; blkIdx < 16U; ++blkIdx)
        {
            uint32_t blkX = (blkIdx & 3U) * 4U;
            uint32_t blkY = (blkIdx >> 2U) * 4U;

            int16_t coeffs[16] = {};
            coeffs[0] = dcCoeffs[blkIdx];

            if (cbpLuma)
            {
                int16_t acCoeffs[16] = {};
                cabacDecodeResidual4x4(cabacEngine_, cabacCtx_.data(), acCoeffs, 15U, 1U);
                for (uint32_t i = 1U; i < 16U; ++i)
                    coeffs[i] = acCoeffs[i - 1U];
                nnzLuma_[mbIdx * 16U + blkIdx] = 1U;
            }

            inverseQuantize4x4(coeffs, qp);
            uint8_t* predPtr = lumaPred + blkY * 16U + blkX;
            uint8_t* outPtr = mbLuma + blkY * yStride + blkX;
            inverseDct4x4AddPred(coeffs, predPtr, 16U, outPtr, yStride);
        }

        decodeChromaCabac(pps, chromaPredMode, cbpChroma, qp, mbX, mbY);
        return true;
    }

    /** Decode P-inter MB with CABAC entropy. */
    void decodeCabacPInterMb(BitReader& br, const Sps& sps, const Pps& pps,
                              int32_t sliceQp, uint32_t mbTypeRaw,
                              Frame& target, const Frame& ref,
                              uint32_t mbX, uint32_t mbY) noexcept
    {
        (void)br; (void)sps;
        // Decode MVD via CABAC
        int16_t mvdX = cabacDecodeMvd(cabacEngine_, cabacCtx_.data(), cCtxMvdX, 0);
        int16_t mvdY = cabacDecodeMvd(cabacEngine_, cabacCtx_.data(), cCtxMvdY, 0);

        // MV prediction (same as CAVLC path)
        MbMotionInfo left    = getMbMotionNeighbor(mbX, mbY, -1, 0);
        MbMotionInfo top     = getMbMotionNeighbor(mbX, mbY, 0, -1);
        MbMotionInfo topRight = getMbMotionNeighbor(mbX, mbY, 1, -1);
        if (!topRight.available) topRight = getMbMotionNeighbor(mbX, mbY, -1, -1);

        MotionVector mvp = computeMvPredictor(left, top, topRight, 0);
        MotionVector mv = { static_cast<int16_t>(mvp.x + mvdX),
                            static_cast<int16_t>(mvp.y + mvdY) };

        uint32_t mbIdx = mbY * widthInMbs_ + mbX;
        mbMotion_[mbIdx] = { mv, 0, true };

        // Motion compensation
        int32_t refX = static_cast<int32_t>(mbX * cMbSize) + (mv.x >> 2);
        int32_t refY = static_cast<int32_t>(mbY * cMbSize) + (mv.y >> 2);
        uint32_t dx = static_cast<uint32_t>(mv.x) & 3U;
        uint32_t dy = static_cast<uint32_t>(mv.y) & 3U;

        uint8_t predLuma[256];
        lumaMotionComp(ref, refX, refY, dx, dy, cMbSize, cMbSize, predLuma, cMbSize);

        int32_t chromaRefX = static_cast<int32_t>(mbX * cChromaBlockSize) + (mv.x >> 3);
        int32_t chromaRefY = static_cast<int32_t>(mbY * cChromaBlockSize) + (mv.y >> 3);
        uint32_t cdx = static_cast<uint32_t>(mv.x) & 7U;
        uint32_t cdy = static_cast<uint32_t>(mv.y) & 7U;

        uint8_t predU[64], predV[64];
        chromaMotionComp(ref, chromaRefX, chromaRefY, cdx, cdy,
                         cChromaBlockSize, cChromaBlockSize, true, predU, cChromaBlockSize);
        chromaMotionComp(ref, chromaRefX, chromaRefY, cdx, cdy,
                         cChromaBlockSize, cChromaBlockSize, false, predV, cChromaBlockSize);

        // CBP via CABAC
        uint8_t cbp = cabacDecodeCbp(cabacEngine_, cabacCtx_.data(),
                                      true, true, false, false);
        uint8_t cbpLuma = cbp & 0x0FU;
        uint8_t cbpChroma = (cbp >> 4U) & 0x03U;

        int32_t qp = sliceQp;
        if (cbp > 0U)
        {
            int32_t qpDelta = cabacDecodeMbQpDelta(cabacEngine_, cabacCtx_.data(), false);
            qp += qpDelta;
            if (qp < 0) qp += 52;
            if (qp > 51) qp -= 52;
        }

        // Luma residual via CABAC
        uint32_t yStride = target.yStride();
        for (uint32_t blkIdx = 0U; blkIdx < 16U; ++blkIdx)
        {
            uint32_t blkX = (blkIdx & 3U) * 4U;
            uint32_t blkY = (blkIdx >> 2U) * 4U;
            uint32_t group8x8 = (blkY >= 8U ? 2U : 0U) + (blkX >= 8U ? 1U : 0U);

            int16_t coeffs[16] = {};
            if ((cbpLuma >> group8x8) & 1U)
            {
                cabacDecodeResidual4x4(cabacEngine_, cabacCtx_.data(), coeffs, 16U, 2U);
                nnzLuma_[mbIdx * 16U + blkIdx] = 1U;
                inverseQuantize4x4(coeffs, qp);
            }

            uint8_t* predPtr = predLuma + blkY * cMbSize + blkX;
            uint8_t* outPtr = target.yMb(mbX, mbY) + blkY * yStride + blkX;
            inverseDct4x4AddPred(coeffs, predPtr, cMbSize, outPtr, yStride);
        }

        // Chroma residual
        int32_t chromaQpIdx = clampQpIdx(qp + pps.chromaQpIndexOffset_);
        int32_t chromaQp = cChromaQpTable[chromaQpIdx];

        int16_t dcCb[4] = {}, dcCr[4] = {};
        if (cbpChroma >= 1U)
        {
            cabacDecodeResidual4x4(cabacEngine_, cabacCtx_.data(), dcCb, 4U, 3U);
            cabacDecodeResidual4x4(cabacEngine_, cabacCtx_.data(), dcCr, 4U, 3U);
            inverseHadamard2x2(dcCb);
            inverseHadamard2x2(dcCr);

            int32_t cqpDiv6 = chromaQp / 6, cqpMod6 = chromaQp % 6;
            int32_t cScale = cDequantScale[cqpMod6][0];
            for (uint32_t i = 0U; i < 4U; ++i)
            {
                if (dcCb[i] != 0) { int32_t v = dcCb[i] * cScale; dcCb[i] = static_cast<int16_t>(cqpDiv6 >= 1 ? v << (cqpDiv6 - 1) : (v + 1) >> 1); }
                if (dcCr[i] != 0) { int32_t v = dcCr[i] * cScale; dcCr[i] = static_cast<int16_t>(cqpDiv6 >= 1 ? v << (cqpDiv6 - 1) : (v + 1) >> 1); }
            }
        }

        uint32_t uvStride = target.uvStride();
        for (uint32_t blkIdx = 0U; blkIdx < 4U; ++blkIdx)
        {
            uint32_t blkX = (blkIdx & 1U) * 4U;
            uint32_t blkY = (blkIdx >> 1U) * 4U;
            int16_t cbCoeffs[16] = {}, crCoeffs[16] = {};
            cbCoeffs[0] = dcCb[blkIdx];
            crCoeffs[0] = dcCr[blkIdx];

            if (cbpChroma >= 2U)
            {
                int16_t acCb[16] = {}, acCr[16] = {};
                cabacDecodeResidual4x4(cabacEngine_, cabacCtx_.data(), acCb, 15U, 4U);
                for (uint32_t i = 1U; i < 16U; ++i) cbCoeffs[i] = acCb[i - 1U];
                inverseQuantize4x4(cbCoeffs, chromaQp);
                cabacDecodeResidual4x4(cabacEngine_, cabacCtx_.data(), acCr, 15U, 4U);
                for (uint32_t i = 1U; i < 16U; ++i) crCoeffs[i] = acCr[i - 1U];
                inverseQuantize4x4(crCoeffs, chromaQp);
            }

            inverseDct4x4AddPred(cbCoeffs, predU + blkY * 8U + blkX, 8U, target.uMb(mbX, mbY) + blkY * uvStride + blkX, uvStride);
            inverseDct4x4AddPred(crCoeffs, predV + blkY * 8U + blkX, 8U, target.vMb(mbX, mbY) + blkY * uvStride + blkX, uvStride);
        }
    }

    /** Decode chroma using CABAC residual. */
    void decodeChromaCabac(const Pps& pps, uint32_t chromaPredMode,
                            uint8_t cbpChroma, int32_t qp,
                            uint32_t mbX, uint32_t mbY) noexcept
    {
        auto chromaMode = static_cast<IntraChromaMode>(chromaPredMode);
        uint8_t predU[64], predV[64];
        intraPredChroma8x8(chromaMode, currentFrame_, mbX, mbY, true, predU);
        intraPredChroma8x8(chromaMode, currentFrame_, mbX, mbY, false, predV);

        int32_t chromaQpIdx = clampQpIdx(qp + pps.chromaQpIndexOffset_);
        int32_t chromaQp = cChromaQpTable[chromaQpIdx];

        int16_t dcCb[4] = {}, dcCr[4] = {};
        if (cbpChroma >= 1U)
        {
            cabacDecodeResidual4x4(cabacEngine_, cabacCtx_.data(), dcCb, 4U, 3U);
            cabacDecodeResidual4x4(cabacEngine_, cabacCtx_.data(), dcCr, 4U, 3U);
            inverseHadamard2x2(dcCb);
            inverseHadamard2x2(dcCr);

            int32_t cqpDiv6 = chromaQp / 6, cqpMod6 = chromaQp % 6;
            int32_t cScale = cDequantScale[cqpMod6][0];
            for (uint32_t i = 0U; i < 4U; ++i)
            {
                if (dcCb[i] != 0) { int32_t v = dcCb[i] * cScale; dcCb[i] = static_cast<int16_t>(cqpDiv6 >= 1 ? v << (cqpDiv6 - 1) : (v + 1) >> 1); }
                if (dcCr[i] != 0) { int32_t v = dcCr[i] * cScale; dcCr[i] = static_cast<int16_t>(cqpDiv6 >= 1 ? v << (cqpDiv6 - 1) : (v + 1) >> 1); }
            }
        }

        uint8_t* mbU = currentFrame_.uMb(mbX, mbY);
        uint8_t* mbV = currentFrame_.vMb(mbX, mbY);
        uint32_t uvStride = currentFrame_.uvStride();
        for (uint32_t blkIdx = 0U; blkIdx < 4U; ++blkIdx)
        {
            uint32_t blkX = (blkIdx & 1U) * 4U;
            uint32_t blkY = (blkIdx >> 1U) * 4U;
            int16_t cbCoeffs[16] = {}, crCoeffs[16] = {};
            cbCoeffs[0] = dcCb[blkIdx]; crCoeffs[0] = dcCr[blkIdx];

            if (cbpChroma >= 2U)
            {
                int16_t acCb[16] = {}, acCr[16] = {};
                cabacDecodeResidual4x4(cabacEngine_, cabacCtx_.data(), acCb, 15U, 4U);
                for (uint32_t i = 1U; i < 16U; ++i) cbCoeffs[i] = acCb[i - 1U];
                inverseQuantize4x4(cbCoeffs, chromaQp);
                cabacDecodeResidual4x4(cabacEngine_, cabacCtx_.data(), acCr, 15U, 4U);
                for (uint32_t i = 1U; i < 16U; ++i) crCoeffs[i] = acCr[i - 1U];
                inverseQuantize4x4(crCoeffs, chromaQp);
            }

            inverseDct4x4AddPred(cbCoeffs, predU + blkY * 8U + blkX, 8U, mbU + blkY * uvStride + blkX, uvStride);
            inverseDct4x4AddPred(crCoeffs, predV + blkY * 8U + blkX, 8U, mbV + blkY * uvStride + blkX, uvStride);
        }
    }
};

} // namespace sub0h264

#endif // CROG_SUB0H264_DECODER_HPP
