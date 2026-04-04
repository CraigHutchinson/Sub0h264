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
#include "decode_timing.hpp"
#include "decode_trace.hpp"
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

    /** Set trace filter for debugging. Printf requires SUB0H264_TRACE=1 build.
     *  Callback tracing is always available. */
    void setTrace(const DecodeTrace& t) noexcept { trace_ = t; }

    /** Access trace for setting callbacks from tests. */
    DecodeTrace& trace() noexcept { return trace_; }

    /** Enable per-section profiling. Pass nullptr to disable. */
    void setProfile(SectionProfile* profile) noexcept { profile_ = profile; }

    /** Read MV at a specific 4x4 block position (absolute 4x4 grid coords).
     *  Public for test inspection of internal MV state. */
    MbMotionInfo motionAt4x4(int32_t blk4x, int32_t blk4y) const noexcept
    {
        return getMotionAt4x4(blk4x, blk4y);
    }

private:
    DecodeTrace trace_;
    SectionProfile* profile_ = nullptr;
    ParamSets paramSets_;
    Frame currentFrame_;
    Frame* activeFrame_ = nullptr; ///< Frame being decoded into (DPB target or currentFrame_)
    Dpb dpb_;
    uint32_t frameCount_ = 0U;
    bool dpbInitialized_ = false;

    // Per-frame MB context: non-zero coefficient counts for CAVLC context
    std::vector<uint8_t> nnzLuma_;    // [mbIdx * 16 + blkIdx]
    std::vector<uint8_t> nnzCb_;      // [mbIdx * 4 + blkIdx]
    std::vector<uint8_t> nnzCr_;      // [mbIdx * 4 + blkIdx]

    // Per-MB luma QP after mb_qp_delta accumulation — ITU-T H.264 §7.4.5.
    // Used by deblocking filter: boundary edges require QP averaging (§8.7.2.2).
    std::vector<int32_t> mbQps_;      // [mbIdx]

    // Per-frame MV context for inter prediction
    std::vector<MbMotionInfo> mbMotion_;  // [mbIdx]

    // Per-MB intra 4x4 prediction modes for MPM derivation across MBs.
    // [mbIdx * 16 + blkIdx] — only valid for I_4x4 MBs.
    // For I_16x16 MBs, all entries set to DC(2) per spec §8.3.1.1.
    std::vector<uint8_t> mbIntra4x4Modes_;  // [mbIdx * 16 + blkIdx]

    // CABAC state
    CabacEngine cabacEngine_;
    std::array<CabacCtx, cNumCabacCtx> cabacCtx_ = {};
    std::vector<bool> mbIsSkip_;   // [mbIdx] — for CABAC skip context
    std::vector<bool> mbIsI4x4_;   // [mbIdx] — for CABAC mb_type context (I_NxN flag)
    std::vector<uint8_t> mbCbp_;   // [mbIdx] — for CABAC CBP context (4 luma bits + 2 chroma)

    uint16_t widthInMbs_ = 0U;
    uint16_t heightInMbs_ = 0U;

    /** Get nC (CAVLC context) for a luma 4x4 block.
     *  nC = (leftNnz + topNnz + 1) >> 1
     */
    /** Get nC context for CAVLC coeff_token decode.
     *  nC = (nA + nB + 1) >> 1 when both available, else nA or nB alone.
     *  Reference: ITU-T H.264 §9.2.1 Table 9-5 context derivation.
     */
    int32_t getLumaNc(uint32_t mbX, uint32_t mbY, uint32_t blkIdx) const noexcept
    {
        // Block position within MB (raster-order 4x4 grid):
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

    /** Get nC (CAVLC context) for a chroma 4x4 block.
     *
     *  Chroma block layout per MB (Cb or Cr, 2x2 grid):
     *    0  1
     *    2  3
     *
     *  @param mbX,mbY    Macroblock position
     *  @param blkIdx     Chroma block index [0-3]
     *  @param isCb       True for Cb plane, false for Cr
     *  @return nC context value
     *
     *  Reference: ITU-T H.264 §9.2.1
     */
    int32_t getChromaNc(uint32_t mbX, uint32_t mbY, uint32_t blkIdx, bool isCb) const noexcept
    {
        const auto& nnz = isCb ? nnzCb_ : nnzCr_;
        uint32_t blkX = blkIdx & 1U;
        uint32_t blkY = blkIdx >> 1U;

        int32_t leftNnz = 0, topNnz = 0;
        bool hasLeft = false, hasTop = false;

        // Left neighbor
        if (blkX > 0U)
        {
            leftNnz = nnz[(mbY * widthInMbs_ + mbX) * 4U + blkIdx - 1U];
            hasLeft = true;
        }
        else if (mbX > 0U)
        {
            /// Right column of left MB: blkY * 2 + 1
            uint32_t leftBlk = blkY * 2U + 1U;
            leftNnz = nnz[(mbY * widthInMbs_ + mbX - 1U) * 4U + leftBlk];
            hasLeft = true;
        }

        // Top neighbor
        if (blkY > 0U)
        {
            topNnz = nnz[(mbY * widthInMbs_ + mbX) * 4U + blkIdx - 2U];
            hasTop = true;
        }
        else if (mbY > 0U)
        {
            /// Bottom row of top MB: 2 + blkX
            uint32_t topBlk = 2U + blkX;
            topNnz = nnz[((mbY - 1U) * widthInMbs_ + mbX) * 4U + topBlk];
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
        mbQps_.resize(totalMbs, 0);
        mbMotion_.resize(totalMbs * 16U); // 16 MVs per MB (per-4x4-block)
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
        std::fill(mbQps_.begin(), mbQps_.end(), 0);
        std::fill(mbMotion_.begin(), mbMotion_.end(), MbMotionInfo{});
        std::fill(mbIntra4x4Modes_.begin(), mbIntra4x4Modes_.end(), static_cast<uint8_t>(2U));

#if SUB0H264_TRACE
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

            // Initialize CABAC contexts — §9.3.1.1
            uint32_t sliceTypeIdx = (sh.sliceType_ == SliceType::I) ? 2U :
                                    (sh.sliceType_ == SliceType::P) ? 0U : 1U;
            initCabacContexts(cabacCtx_.data(), sliceTypeIdx,
                              sh.cabacInitIdc_, sliceQp);

            // Initialize arithmetic engine — §9.3.1.2
            cabacEngine_.init(br);

            // Resize per-MB CABAC tracking vectors
            mbIsSkip_.resize(totalMbs, false);
            std::fill(mbIsSkip_.begin(), mbIsSkip_.end(), false);
            mbIsI4x4_.resize(totalMbs, false);
            std::fill(mbIsI4x4_.begin(), mbIsI4x4_.end(), false);
            mbCbp_.resize(totalMbs, 0U);
            std::fill(mbCbp_.begin(), mbCbp_.end(), static_cast<uint8_t>(0x2FU));
        }

        // Set active frame: decode directly into DPB target when possible.
        // Both I-slices and P-slices decode into the DPB target, eliminating
        // the ~460KB frame copy. For intra-in-P, neighbourhood pixels are
        // already in decodeTarget from previous inter MBs.
        if (decodeTarget && decodeTarget->isAllocated())
            activeFrame_ = decodeTarget;
        else
            activeFrame_ = &currentFrame_;

        // Decode macroblocks
        if (sh.sliceType_ == SliceType::I)
        {
            // QPY accumulates across MBs — ITU-T H.264 §7.4.5.
            int32_t mbQp = sliceQp;
            for (uint32_t mbAddr = sh.firstMbInSlice_; mbAddr < totalMbs; ++mbAddr)
            {
                uint32_t mbX = mbAddr % widthInMbs_;
                uint32_t mbY = mbAddr / widthInMbs_;

                if (useCabac)
                {
                    trace_.onMbStart(mbX, mbY, 200U,
                                     static_cast<uint32_t>(cabacEngine_.bitPosition()));
                    int64_t cT0 = profile_ ? sub0h264TimerUs() : 0;
                    if (!decodeCabacIntraMb(br, *sps, *pps, sh, mbQp, mbX, mbY))
                        break;
                    if (profile_) profile_->intraPredUs += sub0h264TimerUs() - cT0;
                }
                else
                {
                    int64_t intraT0 = profile_ ? sub0h264TimerUs() : 0;
                    if (!decodeIntraMb(br, *sps, *pps, sh, mbQp, mbX, mbY))
                        break;
                    if (profile_) profile_->intraPredUs += sub0h264TimerUs() - intraT0;
                }
                // Store accumulated QP for deblocking pass — ITU-T H.264 §8.7.2.2.
                mbQps_[mbAddr] = mbQp;
            }
        }
        else if (sh.sliceType_ == SliceType::P)
        {
            if (!refFrame)
                return DecodeStatus::Error;


            // QPY accumulates across MBs — ITU-T H.264 §7.4.5.
            int32_t mbQp = sliceQp;

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
                        mbIsI4x4_[mbAddr] = false;
                        int64_t skipT0 = profile_ ? sub0h264TimerUs() : 0;
                        decodePSkipMb(*decodeTarget, *refFrame, mbX, mbY);
                        if (profile_) profile_->interPredUs += sub0h264TimerUs() - skipT0;
                        // Skip MBs inherit QP — no mb_qp_delta per §7.4.5.
                    }
                    else
                    {
                        mbIsSkip_[mbAddr] = false;

                        // §9.3.3.1.2: neighbor I_NxN state for intra suffix context
                        bool leftIsI4x4 = (mbX > 0U) && mbIsI4x4_[mbAddr - 1U];
                        bool topIsI4x4 = (mbY > 0U) && mbIsI4x4_[mbAddr - widthInMbs_];
                        uint32_t mbTypeRaw = cabacDecodeMbTypeP(cabacEngine_, cabacCtx_.data(),
                                                                 leftIsI4x4, topIsI4x4);

                        if (mbTypeRaw >= 5U)
                        {
                            // Intra in P-slice: track I_NxN flag
                            uint32_t intraMbType = mbTypeRaw - 5U;
                            mbIsI4x4_[mbAddr] = (intraMbType == 0U);
                            if (!decodeIntraMbInPSlice(br, *sps, *pps, sh, mbQp,
                                                        intraMbType, *decodeTarget, mbX, mbY))
                                break;
                        }
                        else
                        {
                            mbIsI4x4_[mbAddr] = false;
                            int64_t ciT0 = profile_ ? sub0h264TimerUs() : 0;
                            decodeCabacPInterMb(br, *sps, *pps, mbQp,
                                                mbTypeRaw, *decodeTarget, *refFrame, mbX, mbY);
                            if (profile_) profile_->interPredUs += sub0h264TimerUs() - ciT0;
                        }
                    }
                    // Store accumulated QP for deblocking pass — §8.7.2.2.
                    mbQps_[mbAddr] = mbQp;
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
                        trace_.onMbStart(mbX, mbY, 98U, mbSkipRun); // 98 = skip_run value
                        needSkipRun = false;
                    }

                    if (mbSkipRun > 0U)
                    {
                        trace_.onMbStart(mbX, mbY, 99U, static_cast<uint32_t>(br.bitOffset()));
                        int64_t mcT0 = profile_ ? sub0h264TimerUs() : 0;
                        decodePSkipMb(*decodeTarget, *refFrame, mbX, mbY);
                        if (profile_) profile_->interPredUs += sub0h264TimerUs() - mcT0;
                        --mbSkipRun;
                        // §7.3.4: after skip_run exhausted, next MB is coded
                        // (mb_type follows, NOT another skip_run).
                    }
                    else
                    {
                        trace_.onMbStart(mbX, mbY, 100U, static_cast<uint32_t>(br.bitOffset()));
                        uint32_t mbTypeRaw = br.readUev();
                        needSkipRun = true; // After coded MB, read next skip_run

                        trace_.onMbStart(mbX, mbY, mbTypeRaw, static_cast<uint32_t>(br.bitOffset()));

                        if (mbTypeRaw >= 5U)
                        {
                            mbTypeRaw -= 5U;
                            if (!decodeIntraMbInPSlice(br, *sps, *pps, sh, mbQp,
                                                        mbTypeRaw, *decodeTarget, mbX, mbY))
                                break;
                        }
                        else
                        {
                            int64_t interT0 = profile_ ? sub0h264TimerUs() : 0;
                            decodePInterMb(br, *sps, *pps, mbQp,
                                           mbTypeRaw, *decodeTarget, mbX, mbY,
                                           sh.numRefIdxActiveL0_);
                            if (profile_) profile_->interPredUs += sub0h264TimerUs() - interT0;
                        }
                    }
                    // Store accumulated QP for deblocking pass — §8.7.2.2.
                    mbQps_[mbAddr] = mbQp;
                }
            }
        }


        // Deblocking filter pass (entire frame, after all MBs decoded)
        // Per-MB QP is used; boundary edges use (qpP + qpQ + 1) >> 1 per §8.7.2.2.
        if (pps->deblockingFilterControlPresent_ == 0U ||
            sh.disableDeblockingFilter_ != 1U)
        {
            int64_t dbT0 = profile_ ? sub0h264TimerUs() : 0;
            int32_t alphaOff = sh.sliceAlphaC0Offset_;
            int32_t betaOff = sh.sliceBetaOffset_;

            // activeFrame_ always points to the frame where all MBs were decoded.
            Frame& dbFrame = *activeFrame_;

            for (uint32_t my = 0U; my < heightInMbs_; ++my)
            {
                for (uint32_t mx = 0U; mx < widthInMbs_; ++mx)
                {
                    bool mbIsIntra = (mbMotion_[(my * widthInMbs_ + mx) * 16U].refIdx == -1);
                    deblockMb(dbFrame, mx, my,
                              mbIsIntra, alphaOff, betaOff,
                              nnzLuma_.data(), mbMotion_.data(),
                              mbQps_.data(), pps->chromaQpIndexOffset_,
                              widthInMbs_, heightInMbs_);
                }
            }
            if (profile_) profile_->deblockUs += sub0h264TimerUs() - dbT0;
        }

        // Frame sync: only needed if activeFrame_ differs from decodeTarget.
        // With the activeFrame_ refactor, I-slices decode directly into DPB
        // when possible, eliminating the ~460KB copy.
        int64_t syncT0 = profile_ ? sub0h264TimerUs() : 0;
        if (activeFrame_ != decodeTarget && decodeTarget->isAllocated())
        {
            // Fallback: activeFrame_ was currentFrame_ → copy to DPB
            std::memcpy(decodeTarget->yData(), activeFrame_->yData(),
                        activeFrame_->yStride() * activeFrame_->height());
            std::memcpy(decodeTarget->uData(), activeFrame_->uData(),
                        activeFrame_->uvStride() * (activeFrame_->height() / 2U));
            std::memcpy(decodeTarget->vData(), activeFrame_->vData(),
                        activeFrame_->uvStride() * (activeFrame_->height() / 2U));
        }

        if (profile_) profile_->overheadUs += sub0h264TimerUs() - syncT0;

        // Mark as reference for future P-frames
        if (nal.refIdc != 0U)
            dpb_.markAsReference(sh.frameNum_);

        ++frameCount_;
        if (profile_) ++profile_->frameCount;
        return DecodeStatus::FrameDecoded;
    }

    /** Decode one intra macroblock.
     *  @param currentQp  [in/out] Accumulated QP — updated by mb_qp_delta.
     *                    ITU-T H.264 §7.4.5: QPY is accumulated across MBs.
     */
    bool decodeIntraMb(BitReader& br, const Sps& sps, const Pps& pps,
                       const SliceHeader& sh, int32_t& currentQp,
                       uint32_t mbX, uint32_t mbY) noexcept
    {
        (void)sh;
        if (!br.hasBits(1U))
            return false;

        // Read mb_type
        uint32_t mbTypeRaw = br.readUev();

        // Check for end-of-slice (bitstream exhausted)
        if (br.isExhausted())
            return false;

#if SUB0H264_TRACE
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
        (void)sps;
        uint8_t predMode = i16x16PredMode(static_cast<uint8_t>(mbTypeRaw));
        uint8_t cbpLuma  = i16x16CbpLuma(static_cast<uint8_t>(mbTypeRaw));
        uint8_t cbpChroma = i16x16CbpChroma(static_cast<uint8_t>(mbTypeRaw));

        // Zero NNZ for this MB — blocks not coded retain 0 for correct
        // neighbor context in adjacent MBs' CAVLC decode (§9.2.1).
        {
            uint32_t mbIdx = mbY * widthInMbs_ + mbX;
            std::fill_n(&nnzLuma_[mbIdx * 16U], 16U, static_cast<uint8_t>(0U));
            std::fill_n(&nnzCb_[mbIdx * 4U], 4U, static_cast<uint8_t>(0U));
            std::fill_n(&nnzCr_[mbIdx * 4U], 4U, static_cast<uint8_t>(0U));
        }

        // Intra chroma prediction mode
#if SUB0H264_TRACE
        if (mbY == 0U && (mbX == 0U || mbX == 7U || mbX == 8U))
            std::printf("[DBG]   MB(%lu) Before chroma_pred: bitOff=%lu\n",
                (unsigned long)mbX, (unsigned long)br.bitOffset());
#endif
        uint32_t chromaPredMode = br.readUev();

#if SUB0H264_TRACE
        if (mbY == 0U && (mbX == 0U || mbX == 7U || mbX == 8U))
            std::printf("[DBG]   MB(%lu) After chroma_pred=%lu: bitOff=%lu\n",
                (unsigned long)mbX, (unsigned long)chromaPredMode, (unsigned long)br.bitOffset());
#endif

        // QP delta
        int32_t qpDelta = br.readSev();
#if SUB0H264_TRACE
        if (mbY == 0U && (mbX == 0U || mbX == 7U || mbX == 8U))
            std::printf("[DBG]   MB(%lu) After qpDelta=%d: bitOff=%lu\n",
                (unsigned long)mbX, qpDelta, (unsigned long)br.bitOffset());
#endif
        qp += qpDelta;
        qp = ((qp % 52) + 52) % 52; // Proper modular wrapping for any delta

        // 1. Generate 16x16 luma prediction
        uint8_t lumaPred[256];
        intraPred16x16(static_cast<Intra16x16Mode>(predMode),
                       *activeFrame_, mbX, mbY, lumaPred);

#if SUB0H264_TRACE
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

#if SUB0H264_TRACE
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

#if SUB0H264_TRACE
        if (mbX == 0U && mbY == 0U)
        {
            std::printf("[DBG]   After Hadamard+dequant DC: ");
            for (uint32_t k = 0U; k < 16U; ++k) std::printf("%d ", dcCoeffs[k]);
            std::printf("\n");
        }
#endif

        // 3. Decode and reconstruct each 4x4 luma sub-block
        uint8_t* mbLuma = activeFrame_->yMb(mbX, mbY);
        uint32_t yStride = activeFrame_->yStride();

        for (uint32_t blkIdx = 0U; blkIdx < 16U; ++blkIdx)
        {
            // H.264 §6.4.3: blocks iterate in spec scan order, not raster.
            uint32_t blkX = cLuma4x4BlkX[blkIdx];
            uint32_t blkY = cLuma4x4BlkY[blkIdx];
            uint32_t rasterIdx = cLuma4x4ToRaster[blkIdx];

            int16_t coeffs[16] = {};
            // Hadamard output is in raster order (4x4 grid row-major)
            coeffs[0] = dcCoeffs[rasterIdx]; // DC from Hadamard

            if (cbpLuma)
            {
                // Decode AC coefficients (start from index 1)
                int32_t nc = getLumaNc(mbX, mbY, rasterIdx);
                ResidualBlock4x4 acBlock;
                decodeResidualBlock4x4(br, nc, 15U, 1U, acBlock);

                // Merge AC into coeffs (skip DC at position 0)
                for (uint32_t i = 1U; i < 16U; ++i)
                    coeffs[i] = acBlock.coeffs[i];

                // Store NNZ for context (raster order)
                uint32_t mbIdx = mbY * widthInMbs_ + mbX;
                nnzLuma_[mbIdx * 16U + rasterIdx] = acBlock.totalCoeff;
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
        (void)sps;
        // Read intra 4x4 prediction modes for all 16 blocks.
        // ITU-T H.264 §8.3.1.1: most probable mode = min(leftMode, topMode).
        // If prev_intra4x4_pred_mode_flag=1: use MPM.
        // If flag=0: read rem_intra4x4_pred_mode (3 bits).
        //   If rem < MPM: mode = rem. Else: mode = rem + 1.
        uint8_t predModes[16] = {};

#if SUB0H264_TRACE
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
            // Prediction modes are coded in spec scan order (§6.4.3).
            // Store in raster order for consistent neighbor lookup.
            uint32_t rasterIdx = cLuma4x4ToRaster[i];
            uint8_t leftMode = getNeighborIntra4x4Mode(mbX, mbY, rasterIdx, true, predModes);
            uint8_t topMode  = getNeighborIntra4x4Mode(mbX, mbY, rasterIdx, false, predModes);
            uint8_t mpm = (leftMode < topMode) ? leftMode : topMode;

            uint32_t prevFlag = br.readBit();
            if (prevFlag)
            {
                predModes[rasterIdx] = mpm;
            }
            else
            {
                uint8_t rem = static_cast<uint8_t>(br.readBits(3U));
                predModes[rasterIdx] = (rem < mpm) ? rem : static_cast<uint8_t>(rem + 1U);
            }


#if SUB0H264_TRACE
            if (mbX == 10U && mbY == 0U && i < 4U)
                std::printf("[DBG]   blk%lu(raster%lu): prevFlag=%lu mpm=%u mode=%u\n",
                    (unsigned long)i, (unsigned long)rasterIdx, (unsigned long)prevFlag,
                    mpm, predModes[rasterIdx]);
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

#if SUB0H264_TRACE
        if (mbY == 0U && (mbX == 9U || mbX == 10U))
            std::printf("[DBG] MB(%lu,0) cbpCode=%lu → cbp=0x%02x cbpL=%u cbpC=%u bitOff=%lu\n",
                (unsigned long)mbX, (unsigned long)cbpCode, cbp, cbpLuma, cbpChroma,
                (unsigned long)br.bitOffset());
#endif

        // QP delta (only if CBP > 0)
        if (cbp > 0U)
        {
            int32_t qpDelta = br.readSev();
            qp += qpDelta;
            if (qp < 0) qp += 52;
            if (qp > 51) qp -= 52;
        }

#if SUB0H264_TRACE
        if (mbX == 10U && mbY == 0U)
        {
            std::printf("[DBG] MB(10,0) I4x4: modes=[");
            for (uint32_t k = 0U; k < 16U; ++k) std::printf("%u ", predModes[k]);
            std::printf("] cbp=0x%02x cbpL=%u cbpC=%u qp=%d\n", cbp, cbpLuma, cbpChroma, qp);
        }
#endif

        // Zero NNZ for this MB — blocks not coded will retain 0.
        // ITU-T H.264 §9.2.1: nC context depends on neighboring NNZ.
        // Without this, uncoded blocks in I_4x4 MBs (groups where cbpLuma bit=0)
        // would use stale NNZ from the previous MB's decode, giving wrong nC
        // and causing CAVLC coeff_token misparse.
        {
            uint32_t mbIdx = mbY * widthInMbs_ + mbX;
            std::fill_n(&nnzLuma_[mbIdx * 16U], 16U, static_cast<uint8_t>(0U));
            std::fill_n(&nnzCb_[mbIdx * 4U], 4U, static_cast<uint8_t>(0U));
            std::fill_n(&nnzCr_[mbIdx * 4U], 4U, static_cast<uint8_t>(0U));
        }

        // Decode and reconstruct each 4x4 luma block
        uint8_t* mbLuma = activeFrame_->yMb(mbX, mbY);
        uint32_t yStride = activeFrame_->yStride();

        for (uint32_t blkIdx = 0U; blkIdx < 16U; ++blkIdx)
        {
            // H.264 §6.4.3: iterate in spec scan order, not raster.
            uint32_t blkX = cLuma4x4BlkX[blkIdx];
            uint32_t blkY = cLuma4x4BlkY[blkIdx];
            uint32_t rasterIdx = cLuma4x4ToRaster[blkIdx];

            // Generate 4x4 prediction
            uint8_t pred4x4[16];

            // Get neighbor samples for this 4x4 block
            uint32_t absX = mbX * cMbSize + blkX;
            uint32_t absY = mbY * cMbSize + blkY;

            uint8_t topBuf[8];  // filled below from yRow if absY>0
            uint8_t leftBuf[4]; // filled below from y() if absX>0
            uint8_t topLeftVal = cDefaultPredValue;
            const uint8_t* top = nullptr;
            const uint8_t* topRight = nullptr;
            const uint8_t* left = nullptr;
            const uint8_t* topLeft = nullptr;

            if (absY > 0U)
            {
                const uint8_t* row = activeFrame_->yRow(absY - 1U);
                for (uint32_t i = 0U; i < 4U; ++i) topBuf[i] = row[absX + i];
                top = topBuf;

                // Top-right availability — ITU-T H.264 §6.4.11.
                bool topRightAvailable = (absX + 4U < activeFrame_->width());
                if (cTopRightUnavailScan[blkIdx])
                    topRightAvailable = false;

                if (topRightAvailable)
                {
                    for (uint32_t i = 0U; i < 4U; ++i) topBuf[4 + i] = row[absX + 4U + i];
                    topRight = topBuf + 4U;
                }
            }
            if (absX > 0U)
            {
                // Use stride arithmetic instead of per-row y(x,y) multiply.
                const uint8_t* leftPtr = activeFrame_->yRow(absY) + (absX - 1U);
                uint32_t leftStride = activeFrame_->yStride();
                for (uint32_t i = 0U; i < 4U; ++i)
                    leftBuf[i] = leftPtr[i * leftStride];
                left = leftBuf;
            }
            if (absX > 0U && absY > 0U)
            {
                topLeftVal = activeFrame_->y(absX - 1U, absY - 1U);
                topLeft = &topLeftVal;
            }

            // predModes is stored in raster order
            intraPred4x4(static_cast<Intra4x4Mode>(predModes[rasterIdx]),
                         top, topRight, left, topLeft, pred4x4);

            // Decode residual if CBP indicates this 8x8 group has coefficients
            uint32_t group8x8 = blkIdx >> 2U; // scan order: 0-3=8x8_0, 4-7=8x8_1, ...
            bool hasResidual = (cbpLuma >> group8x8) & 1U;

            int16_t coeffs[16] = {};
            if (hasResidual)
            {
                int32_t nc = getLumaNc(mbX, mbY, rasterIdx);
                ResidualBlock4x4 resBlock;
                decodeResidualBlock4x4(br, nc, cMaxCoeff4x4, 0U, resBlock);

#if SUB0H264_TRACE
                if (mbX == 9U && mbY == 0U)
                {
                    std::printf("[DBG]   MB9 luma scan%lu(raster%lu): nC=%d tc=%u bits=%lu\n",
                        (unsigned long)blkIdx, (unsigned long)rasterIdx,
                        nc, resBlock.totalCoeff,
                        (unsigned long)(br.bitOffset() - blkBitBefore));
                    if (blkIdx >= 12U)
                    {
                        std::printf("[DBG]     raw coeffs=[");
                        for (uint32_t k = 0U; k < 16U; ++k)
                            std::printf("%d ", resBlock.coeffs[k]);
                        std::printf("]\n");
                    }
                }
#endif

                for (uint32_t i = 0U; i < 16U; ++i)
                    coeffs[i] = resBlock.coeffs[i];

                uint32_t mbIdx = mbY * widthInMbs_ + mbX;
                nnzLuma_[mbIdx * 16U + rasterIdx] = resBlock.totalCoeff;

                inverseQuantize4x4(coeffs, qp);
            }

#if SUB0H264_TRACE
            if (mbX == 9U && mbY == 0U && blkIdx >= 12U)
            {
                std::printf("[DBG]   MB9 scan%lu(raster%lu) pos(%lu,%lu): mode=%u pred=[%u %u %u %u] hasRes=%d qp=%d\n",
                    (unsigned long)blkIdx, (unsigned long)rasterIdx,
                    (unsigned long)blkX, (unsigned long)blkY,
                    predModes[rasterIdx],
                    pred4x4[0], pred4x4[1], pred4x4[2], pred4x4[3], hasResidual, qp);
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

#if SUB0H264_TRACE
            if (mbX == 9U && mbY == 0U && blkIdx >= 12U)
            {
                std::printf("[DBG]   output row0=[%u %u %u %u] row3=[%u %u %u %u]\n",
                    outPtr[0], outPtr[1], outPtr[2], outPtr[3],
                    outPtr[3*yStride], outPtr[3*yStride+1],
                    outPtr[3*yStride+2], outPtr[3*yStride+3]);
            }
#endif
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
#if SUB0H264_TRACE
        if (mbY == 0U && (mbX < 2U || mbX == 9U))
            std::printf("[DBG] MB(%lu,0) chromaMb START bitOff=%lu cbpC=%u\n",
                (unsigned long)mbX, (unsigned long)br.bitOffset(), cbpChroma);
#endif
        auto chromaMode = static_cast<IntraChromaMode>(chromaPredMode);

        // Generate chroma predictions (8x8 for each plane)
        uint8_t predU[64], predV[64];
        intraPredChroma8x8(chromaMode, *activeFrame_, mbX, mbY, true, predU);
        intraPredChroma8x8(chromaMode, *activeFrame_, mbX, mbY, false, predV);

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

            // Trace raw CAVLC-decoded DC coefficients (before Hadamard)
            if (trace_.shouldTrace(mbX, mbY))
            {
                trace_.onChromaDcRaw(mbX, mbY, dcCb, dcCr);
            }

            inverseHadamard2x2(dcCb);
            inverseHadamard2x2(dcCr);

            // Dequantize chroma DC — ITU-T H.264 §8.5.12.1.
            // For 4:2:0 chroma DC (2x2 Hadamard), the combined formula is:
            //   dcC = (f * LevelScale * 16 << qpDiv6) >> 5 = f * LevelScale << (qpDiv6 - 1)
            // The factor of 16 comes from the flat scaling matrix (weight_scale=16).
            // Reference: libavc ih264d_cavlc_parse_chroma_dc:
            //   scale_u = quant_scale[0] << qp_div6;  (where quant_scale = LevelScale * 16)
            //   dc = (f * scale_u) >> 5;
            // Verified: for cqpDiv6=2, LevelScale=10: (f * 10 * 16 << 2) >> 5 = f * 10 << 1.
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

            if (trace_.shouldTrace(mbX, mbY))
                trace_.onChromaDcDequant(mbX, mbY, dcCb, dcCr);
        }

        // ITU-T H.264 §7.3.5 residual_cavlc: all Cb AC blocks first, then all Cr AC blocks.
        // The outer loop over iCbCr (0=Cb, 1=Cr) is separate from the reconstruction loop.
        uint32_t mbIdx = mbY * widthInMbs_ + mbX;

        // Stash per-block AC coefficients so we can separate decode from reconstruct.
        int16_t cbCoeffsBuf[4][16] = {};
        int16_t crCoeffsBuf[4][16] = {};

        // DC-only init from Hadamard output
        for (uint32_t blkIdx = 0U; blkIdx < 4U; ++blkIdx)
        {
            cbCoeffsBuf[blkIdx][0] = dcCb[blkIdx];
            crCoeffsBuf[blkIdx][0] = dcCr[blkIdx];
        }

        // Decode all Cb AC blocks (iCbCr=0), then all Cr AC blocks (iCbCr=1).
        // Reference: ITU-T H.264 §7.3.5 residual_cavlc() syntax table.
        if (cbpChroma >= 2U)
        {
            for (uint32_t blkIdx = 0U; blkIdx < 4U; ++blkIdx)
            {
                int32_t ncCb = getChromaNc(mbX, mbY, blkIdx, true);
                ResidualBlock4x4 acBlock;
                decodeResidualBlock4x4(br, ncCb, 15U, 1U, acBlock);
                for (uint32_t i = 1U; i < 16U; ++i)
                    cbCoeffsBuf[blkIdx][i] = acBlock.coeffs[i];
                nnzCb_[mbIdx * 4U + blkIdx] = acBlock.totalCoeff;
                // Save DC (already dequanted by Hadamard path), dequant AC, restore DC.
                // ITU-T H.264 §8.5.12.1: DC was scaled in the Hadamard dequant above.
                int16_t savedDcCb = cbCoeffsBuf[blkIdx][0];
                inverseQuantize4x4(cbCoeffsBuf[blkIdx], chromaQp);
                cbCoeffsBuf[blkIdx][0] = savedDcCb;
            }
            for (uint32_t blkIdx = 0U; blkIdx < 4U; ++blkIdx)
            {
                int32_t ncCr = getChromaNc(mbX, mbY, blkIdx, false);
                ResidualBlock4x4 acBlock;
                decodeResidualBlock4x4(br, ncCr, 15U, 1U, acBlock);
                for (uint32_t i = 1U; i < 16U; ++i)
                    crCoeffsBuf[blkIdx][i] = acBlock.coeffs[i];
                nnzCr_[mbIdx * 4U + blkIdx] = acBlock.totalCoeff;
                int16_t savedDcCr = crCoeffsBuf[blkIdx][0];
                inverseQuantize4x4(crCoeffsBuf[blkIdx], chromaQp);
                crCoeffsBuf[blkIdx][0] = savedDcCr;
            }
        }

        // Reconstruct chroma 4x4 blocks using buffered coefficients.
        uint8_t* mbU = activeFrame_->uMb(mbX, mbY);
        uint8_t* mbV = activeFrame_->vMb(mbX, mbY);
        uint32_t uvStride = activeFrame_->uvStride();

        for (uint32_t blkIdx = 0U; blkIdx < 4U; ++blkIdx)
        {
            uint32_t blkX = (blkIdx & 1U) * 4U;
            uint32_t blkY = (blkIdx >> 1U) * 4U;

            uint8_t* predCbPtr = predU + blkY * 8U + blkX;
            uint8_t* outCbPtr  = mbU + blkY * uvStride + blkX;
            inverseDct4x4AddPred(cbCoeffsBuf[blkIdx], predCbPtr, 8U, outCbPtr, uvStride);

            uint8_t* predCrPtr = predV + blkY * 8U + blkX;
            uint8_t* outCrPtr  = mbV + blkY * uvStride + blkX;
            inverseDct4x4AddPred(crCoeffsBuf[blkIdx], predCrPtr, 8U, outCrPtr, uvStride);
        }
    }

    // ── P-frame decode methods ──────────────────────────────────────────

    /** Fill all 16 4x4 blocks of an MB with the same MV. */
    void setMbMotion(uint32_t mbIdx, MotionVector mv, int8_t refIdx) noexcept
    {
        MbMotionInfo info = { mv, refIdx, true };
        for (uint32_t i = 0U; i < 16U; ++i)
            mbMotion_[mbIdx * 16U + i] = info;
    }

    /** Fill a partition's 4x4 blocks with MV.
     *  For 16x8: part 0 = rows 0-1 (8 blocks), part 1 = rows 2-3 (8 blocks).
     *  For 8x16: part 0 = cols 0-1 (8 blocks), part 1 = cols 2-3 (8 blocks).
     */
    void setPartitionMotion(uint32_t mbIdx, uint32_t mbType, uint32_t partIdx,
                            MotionVector mv, int8_t refIdx) noexcept
    {
        MbMotionInfo info = { mv, refIdx, true };
        if (mbType == 1U) // 16x8
        {
            uint32_t startRow = partIdx * 2U;
            for (uint32_t r = startRow; r < startRow + 2U; ++r)
                for (uint32_t c = 0U; c < 4U; ++c)
                    mbMotion_[mbIdx * 16U + r * 4U + c] = info;
        }
        else if (mbType == 2U) // 8x16
        {
            uint32_t startCol = partIdx * 2U;
            for (uint32_t r = 0U; r < 4U; ++r)
                for (uint32_t c = startCol; c < startCol + 2U; ++c)
                    mbMotion_[mbIdx * 16U + r * 4U + c] = info;
        }
        else // fallback: fill all
        {
            setMbMotion(mbIdx, mv, refIdx);
        }
    }

    /** Get MV at a specific 4x4 block position (absolute pixel coords / 4).
     *  @param blk4x blk4y  Block position in 4x4 units (0 to width/4-1, etc.)
     */
    MbMotionInfo getMotionAt4x4(int32_t blk4x, int32_t blk4y) const noexcept
    {
        int32_t maxX = static_cast<int32_t>(widthInMbs_) * 4 - 1;
        int32_t maxY = static_cast<int32_t>(heightInMbs_) * 4 - 1;
        if (blk4x < 0 || blk4y < 0 || blk4x > maxX || blk4y > maxY)
            return {};
        uint32_t mbIdx = (blk4y / 4) * widthInMbs_ + (blk4x / 4);
        uint32_t blkIdx = (blk4y % 4) * 4 + (blk4x % 4);
        return mbMotion_[mbIdx * 16U + blkIdx];
    }

    /** Get MV neighbor info for a macroblock (legacy — uses bottom-right 4x4). */
    MbMotionInfo getMbMotionNeighbor(uint32_t mbX, uint32_t mbY, int32_t dx, int32_t dy) const noexcept
    {
        int32_t nx = static_cast<int32_t>(mbX) + dx;
        int32_t ny = static_cast<int32_t>(mbY) + dy;
        if (nx < 0 || ny < 0 || nx >= widthInMbs_ || ny >= heightInMbs_)
            return {};
        // For left neighbor: read right column (blk col 3). For top: bottom row (blk row 3).
        // For topRight: bottom-left block.
        uint32_t blkRow = (dy == -1) ? 3U : 0U; // top neighbor → bottom row of above MB
        uint32_t blkCol = (dx == -1) ? 3U : 0U; // left neighbor → right col of left MB
        if (dx == 1 && dy == -1) { blkRow = 3U; blkCol = 0U; } // topRight → bottom-left
        if (dx == -1 && dy == -1) { blkRow = 3U; blkCol = 3U; } // topLeft → bottom-right
        uint32_t mbIdx = ny * widthInMbs_ + nx;
        uint32_t blkIdx = blkRow * 4U + blkCol;
        return mbMotion_[mbIdx * 16U + blkIdx];
    }

    /** Decode a P_Skip macroblock: inferred MV, no residual.
     *
     *  VALIDATED: §8.4.1.1 zero-MV condition matches spec (unavailable neighbor
     *  → MV=0). MC full-pel copy verified pixel-exact vs IDR reference.
     *  KNOWN ISSUE: P-frame row 2+ chroma diffs vs ffmpeg/libavc. Our output
     *  matches IDR at MV=(0,0); reference decoders produce output consistent
     *  with MV=(16,8). Likely a bitstream alignment issue in preceding MB
     *  residual parsing that shifts the skip_run read position.
     */
    void decodePSkipMb(Frame& target, const Frame& ref,
                        uint32_t mbX, uint32_t mbY) noexcept
    {
        // Neighbor derivation — §6.4.11.7 for 16x16 partition, mbPartIdx=0.
        // VALIDATED: getMbMotionNeighbor reads correct 4x4 block per partition layout.
        MbMotionInfo left    = getMbMotionNeighbor(mbX, mbY, -1, 0);
        MbMotionInfo top     = getMbMotionNeighbor(mbX, mbY, 0, -1);
        MbMotionInfo topRight = getMbMotionNeighbor(mbX, mbY, 1, -1);
        if (!topRight.available)
            topRight = getMbMotionNeighbor(mbX, mbY, -1, -1); // D fallback §8.4.1.3

        // §8.4.1.1: If mbAddrA or mbAddrB unavailable, or either has (ref=0,MV=0,0),
        // then skip MV = (0,0). Otherwise, MV = median predictor from §8.4.1.3.
        // VALIDATED against spec text. Produces MV=(0,0) when left unavailable (mbX=0).
        MotionVector skipMv = {0, 0};
        if (left.available && top.available &&
            !(left.refIdx == 0 && left.mv.x == 0 && left.mv.y == 0) &&
            !(top.refIdx == 0 && top.mv.x == 0 && top.mv.y == 0))
        {
            skipMv = computeMvPredictor(left, top, topRight, 0);
        }

        // Store MV for this MB (all 16 blocks same MV for skip)
        uint32_t mbIdx = mbY * widthInMbs_ + mbX;
        setMbMotion(mbIdx, skipMv, 0);

        // Motion compensation: copy 16x16 block from reference
        int32_t refX = static_cast<int32_t>(mbX * cMbSize) + (skipMv.x >> 2);
        int32_t refY = static_cast<int32_t>(mbY * cMbSize) + (skipMv.y >> 2);
        uint32_t dx = static_cast<uint32_t>(skipMv.x) & 3U;
        uint32_t dy = static_cast<uint32_t>(skipMv.y) & 3U;

        lumaMotionComp(ref, refX, refY, dx, dy, cMbSize, cMbSize,
                        target.yMb(mbX, mbY), target.yStride());

        // Chroma — derive from luma MV (divide by 2, eighth-pel)
        int32_t chromaRefX = static_cast<int32_t>(mbX * cChromaBlockSize) + (skipMv.x >> 3);
        int32_t chromaRefY = static_cast<int32_t>(mbY * cChromaBlockSize) + (skipMv.y >> 3);
        uint32_t cdx = static_cast<uint32_t>(skipMv.x) & 7U;
        uint32_t cdy = static_cast<uint32_t>(skipMv.y) & 7U;

        // Diagnostic: verify ref frame chroma at MC position for debugging
        chromaMotionComp(ref, chromaRefX, chromaRefY, cdx, cdy,
                         cChromaBlockSize, cChromaBlockSize, true,
                         target.uMb(mbX, mbY), target.uvStride());
        chromaMotionComp(ref, chromaRefX, chromaRefY, cdx, cdy,
                         cChromaBlockSize, cChromaBlockSize, false,
                         target.vMb(mbX, mbY), target.uvStride());

        // NNZ = 0 for skip MBs
        std::fill_n(&nnzLuma_[mbIdx * 16U], 16U, static_cast<uint8_t>(0U));
    }

    /** Decode P-inter macroblock — §7.3.5 mb_pred / sub_mb_pred.
     *
     *  Handles P_L0_16x16, P_L0_L0_16x8, P_L0_L0_8x16, P_8x8, P_8x8ref0.
     *
     *  VALIDATED: Bitstream parsing bit-aligned with libavc through MB(4,0).
     *  VALIDATED: P_8x8 sub-partition MVDs correctly stored (shadow var fix).
     *  VALIDATED: 8x16/16x8 directional shortcuts per §8.4.1.3.1.
     *  VALIDATED: Per-4x4-block MV storage for partition-aware neighbor derivation.
     *  KNOWN ISSUE: Residual bit consumption may be off by ~1 bit for some
     *  chroma AC blocks, causing skip_run misalignment after row 1.
     *
     *  @param mbQp  [in/out] Accumulated QP — ITU-T H.264 §7.4.5.
     */
    void decodePInterMb(BitReader& br, const Sps& sps, const Pps& pps,
                         int32_t& mbQp, uint32_t mbTypeRaw,
                         Frame& target, uint32_t mbX, uint32_t mbY,
                         uint8_t numRefIdxL0Active = 1U) noexcept
    {
        (void)sps;
        // Trace numRefIdxL0Active for first MB of each frame
        if (mbX == 0U && mbY == 0U)
            trace_.onBlockResidual(mbX, mbY, 95U, numRefIdxL0Active, mbTypeRaw, 0U);
        // mbTypeRaw: 0=P_L0_16x16, 1=P_L0_L0_16x8, 2=P_L0_L0_8x16, 3=P_8x8, 4=P_8x8ref0

        // ITU-T H.264 §7.3.5.1: mb_pred for P-inter MBs.
        // Partition types: 0=16x16, 1=16x8, 2=8x16, 3=8x8, 4=8x8ref0
        // Number of partitions: 1 for 16x16, 2 for 16x8 and 8x16.
        // 8x8 and 8x8ref0 use sub_mb_pred (not yet supported).
        // ITU-T H.264 §7.3.5.1: mb_pred / sub_mb_pred for P-inter MBs.
        // 0=P_L0_16x16 (1 partition), 1=P_L0_L0_16x8 (2), 2=P_L0_L0_8x16 (2)
        // 3=P_8x8 (4 sub-MBs), 4=P_8x8ref0 (4 sub-MBs, ref=0)
        int16_t mvdX[4] = {}, mvdY[4] = {};
        int16_t subMvdX[4] = {}, subMvdY[4] = {}; // Per-8x8 MVDs for P_8x8
        uint8_t refIdxL0[4] = {};  // Per-partition ref_idx (0-based)
        uint32_t numParts = 1U;

        if (mbTypeRaw <= 2U)
        {
            // 16x16, 16x8, or 8x16: 1 or 2 partitions
            numParts = (mbTypeRaw == 0U) ? 1U : 2U;

            // Read ref_idx for each partition — §7.3.5.1
            for (uint32_t p = 0U; p < numParts; ++p)
            {
                if (numRefIdxL0Active > 1U)
                {
                    if (numRefIdxL0Active == 2U)
                        refIdxL0[p] = static_cast<uint8_t>(1U - br.readBit()); // te(v) range=1 §9.1
                    else
                        refIdxL0[p] = static_cast<uint8_t>(br.readUev()); // te(v) range>1
                }
            }

            // Read MVD for each partition
            for (uint32_t p = 0U; p < numParts; ++p)
            {
                mvdX[p] = static_cast<int16_t>(br.readSev());
                mvdY[p] = static_cast<int16_t>(br.readSev());
            }
        }
        else
        {
            // P_8x8 or P_8x8ref0: sub_mb_pred — §7.3.5.2
            // Read 4 sub_mb_type values
            uint32_t subMbType[4];
            for (uint32_t s = 0U; s < 4U; ++s)
                subMbType[s] = br.readUev();

            // Read ref_idx for each 8x8 partition — §7.3.5.2
            // P_8x8ref0: all ref_idx = 0 (not read from bitstream).
            if (mbTypeRaw == 3U) // P_8x8 (not ref0)
            {
                for (uint32_t s = 0U; s < 4U; ++s)
                {
                    if (numRefIdxL0Active > 1U)
                    {
                        if (numRefIdxL0Active == 2U)
                            refIdxL0[s] = static_cast<uint8_t>(1U - br.readBit()); // te(v) §9.1
                        else
                            refIdxL0[s] = static_cast<uint8_t>(br.readUev());
                    }
                }
            }

            // Read MVD for each sub-partition — §7.3.5.2.
            // sub_mb_type: 0=8x8(1 MVD), 1=8x4(2), 2=4x8(2), 3=4x4(4)
            // Store first MVD per 8x8 block; consume rest for bit alignment.
            for (uint32_t s = 0U; s < 4U; ++s)
            {
                uint32_t numSubParts = 1U;
                if (subMbType[s] == 1U || subMbType[s] == 2U) numSubParts = 2U;
                else if (subMbType[s] == 3U) numSubParts = 4U;

                for (uint32_t sp = 0U; sp < numSubParts; ++sp)
                {
                    int16_t dx = static_cast<int16_t>(br.readSev());
                    int16_t dy = static_cast<int16_t>(br.readSev());
                    if (sp == 0U) { subMvdX[s] = dx; subMvdY[s] = dy; }
                }
            }
        }

        // Compute per-partition MV predictors — ITU-T H.264 §8.4.1.3.
        // Use per-4x4-block neighbor lookups for partition-aware prediction.
        int32_t mb4x = static_cast<int32_t>(mbX) * 4;
        int32_t mb4y = static_cast<int32_t>(mbY) * 4;

        MotionVector mvPart[2];
        MotionVector mvSub8x8[4] = {}; // Per-8x8 MVs for P_8x8
        if (mbTypeRaw <= 2U && numParts == 1U)
        {
            // P_L0_16x16: A=left(col-1,row0), B=top(col0,row-1), C=topRight(col4,row-1)
            MbMotionInfo a = getMotionAt4x4(mb4x - 1, mb4y);
            MbMotionInfo b = getMotionAt4x4(mb4x, mb4y - 1);

            MbMotionInfo c = getMotionAt4x4(mb4x + 4, mb4y - 1);
            if (!c.available)
                c = getMotionAt4x4(mb4x - 1, mb4y - 1); // top-left fallback
            MotionVector mvp = computeMvPredictor(a, b, c, static_cast<int8_t>(refIdxL0[0]));
            mvPart[0] = { static_cast<int16_t>(mvp.x + mvdX[0]),
                          static_cast<int16_t>(mvp.y + mvdY[0]) };
            trace_.onMvPrediction(mbX, mbY, 0, mvp, {mvdX[0], mvdY[0]}, mvPart[0], a, b, c);
        }
        else if (mbTypeRaw == 1U)
        {
            // P_L0_L0_16x8: §8.4.1.3.1
            // Partition 0 (top 16x8): B=top neighbor directly if same refIdx
            {
                MbMotionInfo a0 = getMotionAt4x4(mb4x - 1, mb4y);
                MbMotionInfo b0 = getMotionAt4x4(mb4x, mb4y - 1);
                MbMotionInfo c0 = getMotionAt4x4(mb4x + 4, mb4y - 1);
                if (!c0.available) c0 = getMotionAt4x4(mb4x - 1, mb4y - 1);
                int8_t ri0 = static_cast<int8_t>(refIdxL0[0]);
                MotionVector mvp0;
                if (b0.available && b0.refIdx == ri0)
                    mvp0 = b0.mv; // directional shortcut — §8.4.1.3.1
                else
                    mvp0 = computeMvPredictor(a0, b0, c0, ri0);
                mvPart[0] = { static_cast<int16_t>(mvp0.x + mvdX[0]),
                              static_cast<int16_t>(mvp0.y + mvdY[0]) };
                trace_.onMvPrediction(mbX, mbY, 0, mvp0, {mvdX[0], mvdY[0]}, mvPart[0], a0, b0, c0);
            }

            // Partition 1 (bottom 16x8): §8.4.1.3.1 directional shortcut: use LEFT.
            {
                MbMotionInfo a1 = getMotionAt4x4(mb4x - 1, mb4y + 2); // left at row 2
                MbMotionInfo b1 = { mvPart[0], static_cast<int8_t>(refIdxL0[0]), true }; // partition 0
                // C = top-right of partition at (16, 7) relative to MB → unavailable.
                MbMotionInfo c1 = getMotionAt4x4(mb4x + 4, mb4y + 1);
                if (!c1.available)
                {
                    // D = top-left of partition at (-1, 7) → left MB's (col 3, row 1). §8.4.1.3
                    c1 = getMotionAt4x4(mb4x - 1, mb4y + 1);
                }
                int8_t ri1 = static_cast<int8_t>(refIdxL0[1]);
                MotionVector mvp1;
                if (a1.available && a1.refIdx == ri1)
                    mvp1 = a1.mv; // directional shortcut — §8.4.1.3.1
                else
                    mvp1 = computeMvPredictor(a1, b1, c1, ri1);
                mvPart[1] = { static_cast<int16_t>(mvp1.x + mvdX[1]),
                              static_cast<int16_t>(mvp1.y + mvdY[1]) };
                trace_.onMvPrediction(mbX, mbY, 1, mvp1, {mvdX[1], mvdY[1]}, mvPart[1], a1, b1, c1);
            }
        }
        else if (mbTypeRaw == 2U)
        {
            // P_L0_L0_8x16: §8.4.1.3.1
            // Partition 0 (left 8x16): A=left neighbor directly if same refIdx
            {
                MbMotionInfo a0 = getMotionAt4x4(mb4x - 1, mb4y);
                MbMotionInfo b0 = getMotionAt4x4(mb4x, mb4y - 1);
                MbMotionInfo c0 = getMotionAt4x4(mb4x + 2, mb4y - 1); // top of right half
                if (!c0.available) c0 = getMotionAt4x4(mb4x - 1, mb4y - 1);
                int8_t ri0 = static_cast<int8_t>(refIdxL0[0]);
                MotionVector mvp0;
                if (a0.available && a0.refIdx == ri0)
                    mvp0 = a0.mv; // directional shortcut — §8.4.1.3.1
                else
                    mvp0 = computeMvPredictor(a0, b0, c0, ri0);
                mvPart[0] = { static_cast<int16_t>(mvp0.x + mvdX[0]),
                              static_cast<int16_t>(mvp0.y + mvdY[0]) };
                trace_.onMvPrediction(mbX, mbY, 0, mvp0, {mvdX[0], mvdY[0]}, mvPart[0], a0, b0, c0);
            }

            // Partition 1 (right 8x16): C=topRight directly if same refIdx
            {
                MbMotionInfo a1 = { mvPart[0], static_cast<int8_t>(refIdxL0[0]), true };
                MbMotionInfo b1 = getMotionAt4x4(mb4x + 2, mb4y - 1);
                MbMotionInfo c1 = getMotionAt4x4(mb4x + 4, mb4y - 1);
                if (!c1.available)
                    c1 = getMotionAt4x4(mb4x + 1, mb4y - 1); // D fallback
                int8_t ri1 = static_cast<int8_t>(refIdxL0[1]);
                MotionVector mvp1;
                if (c1.available && c1.refIdx == ri1)
                    mvp1 = c1.mv; // directional shortcut — §8.4.1.3.1
                else
                    mvp1 = computeMvPredictor(a1, b1, c1, ri1);
                mvPart[1] = { static_cast<int16_t>(mvp1.x + mvdX[1]),
                              static_cast<int16_t>(mvp1.y + mvdY[1]) };
                trace_.onMvPrediction(mbX, mbY, 1, mvp1, {mvdX[1], mvdY[1]}, mvPart[1], a1, b1, c1);
            }
        }
        else
        {
            // P_8x8/P_8x8ref0: per-8x8 sub-partition MV prediction — §8.4.1.3.
            // 4 sub-partitions in raster order: TL(0), TR(1), BL(2), BR(3).
            // Each 8x8 sub-partition has its own MV = MVP + MVD.
            // Store MVs immediately so subsequent sub-partitions can use them.
            // Sub-partition origins in 4x4 block coordinates:
            //   sub0=(mb4x,   mb4y),   sub1=(mb4x+2, mb4y)
            //   sub2=(mb4x,   mb4y+2), sub3=(mb4x+2, mb4y+2)
            static constexpr int32_t subOffX[4] = {0, 2, 0, 2};
            static constexpr int32_t subOffY[4] = {0, 0, 2, 2};

            uint32_t mbIdx = mbY * widthInMbs_ + mbX;
            for (uint32_t s = 0U; s < 4U; ++s)
            {
                int32_t sx = mb4x + subOffX[s];
                int32_t sy = mb4y + subOffY[s];

                // Neighbors for this 8x8 sub-partition — §8.4.1.3
                MbMotionInfo a = getMotionAt4x4(sx - 1, sy);
                MbMotionInfo b = getMotionAt4x4(sx, sy - 1);
                MbMotionInfo c = getMotionAt4x4(sx + 2, sy - 1);
                if (!c.available)
                    c = getMotionAt4x4(sx - 1, sy - 1); // D fallback

                MotionVector mvp = computeMvPredictor(a, b, c, static_cast<int8_t>(refIdxL0[s]));
                MotionVector mv = { static_cast<int16_t>(mvp.x + subMvdX[s]),
                                    static_cast<int16_t>(mvp.y + subMvdY[s]) };
                trace_.onMvPrediction(mbX, mbY, s, mvp, {subMvdX[s], subMvdY[s]}, mv, a, b, c);

                // Store this sub-partition's MV in its 4 blocks (2x2 in 4x4 grid)
                MbMotionInfo info = { mv, static_cast<int8_t>(refIdxL0[s]), true };
                for (uint32_t r = 0U; r < 2U; ++r)
                    for (uint32_t cc = 0U; cc < 2U; ++cc)
                        mbMotion_[mbIdx * 16U + (subOffY[s] + r) * 4U + subOffX[s] + cc] = info;

                mvSub8x8[s] = mv;
            }
            // Per-sub-partition MVs already stored above.
        }

        // Store per-4x4-block MVs with actual ref_idx for neighbor derivation.
        uint32_t mbIdx = mbY * widthInMbs_ + mbX;
        if (mbTypeRaw <= 2U)
        {
            if (numParts == 1U)
                setMbMotion(mbIdx, mvPart[0], static_cast<int8_t>(refIdxL0[0]));
            else
            {
                setPartitionMotion(mbIdx, mbTypeRaw, 0U, mvPart[0], static_cast<int8_t>(refIdxL0[0]));
                setPartitionMotion(mbIdx, mbTypeRaw, 1U, mvPart[1], static_cast<int8_t>(refIdxL0[1]));
            }
        }
        // P_8x8 MVs were stored incrementally in the sub-partition loop above.


        // Per-partition motion compensation — ITU-T H.264 §8.4.2
        // Reference frame lookup per partition via DPB L0 list.
        auto getRef = [this](uint8_t idx) -> const Frame& {
            const Frame* f = dpb_.getReference(idx);
            // Fall back to most recent ref if requested index exceeds DPB size.
            // This handles cases where numRefIdxL0Active (from PPS/override)
            // exceeds the actual number of references in the DPB (short GOPs).
            if (!f) f = dpb_.getReference(0U);
            return *f;
        };

        uint8_t predLuma[256];
        uint8_t predU[64], predV[64];

        if (mbTypeRaw <= 2U && numParts == 1U)
        {
            // P_L0_16x16: single 16x16 partition
            const Frame& ref = getRef(refIdxL0[0]);
            MotionVector& mv = mvPart[0];
            int32_t refX = static_cast<int32_t>(mbX * cMbSize) + (mv.x >> 2);
            int32_t refY = static_cast<int32_t>(mbY * cMbSize) + (mv.y >> 2);
            lumaMotionComp(ref, refX, refY, static_cast<uint32_t>(mv.x) & 3U,
                           static_cast<uint32_t>(mv.y) & 3U,
                           cMbSize, cMbSize, predLuma, cMbSize);

            int32_t cRefX = static_cast<int32_t>(mbX * cChromaBlockSize) + (mv.x >> 3);
            int32_t cRefY = static_cast<int32_t>(mbY * cChromaBlockSize) + (mv.y >> 3);
            chromaMotionComp(ref, cRefX, cRefY, static_cast<uint32_t>(mv.x) & 7U,
                             static_cast<uint32_t>(mv.y) & 7U,
                             cChromaBlockSize, cChromaBlockSize, true, predU, cChromaBlockSize);
            chromaMotionComp(ref, cRefX, cRefY, static_cast<uint32_t>(mv.x) & 7U,
                             static_cast<uint32_t>(mv.y) & 7U,
                             cChromaBlockSize, cChromaBlockSize, false, predV, cChromaBlockSize);
        }
        else if (mbTypeRaw == 1U)
        {
            // P_L0_L0_16x8: two 16x8 partitions (top half, bottom half)
            for (uint32_t p = 0U; p < 2U; ++p)
            {
                const Frame& ref = getRef(refIdxL0[p]);
                MotionVector& mv = mvPart[p];
                uint32_t partOffY = p * 8U;
                int32_t refX = static_cast<int32_t>(mbX * cMbSize) + (mv.x >> 2);
                int32_t refY = static_cast<int32_t>(mbY * cMbSize + partOffY) + (mv.y >> 2);
                lumaMotionComp(ref, refX, refY,
                               static_cast<uint32_t>(mv.x) & 3U,
                               static_cast<uint32_t>(mv.y) & 3U,
                               16U, 8U,
                               predLuma + partOffY * cMbSize, cMbSize);

                uint32_t cPartOffY = p * 4U;
                int32_t cRefX = static_cast<int32_t>(mbX * cChromaBlockSize) + (mv.x >> 3);
                int32_t cRefY = static_cast<int32_t>(mbY * cChromaBlockSize + cPartOffY) + (mv.y >> 3);
                chromaMotionComp(ref, cRefX, cRefY,
                                 static_cast<uint32_t>(mv.x) & 7U,
                                 static_cast<uint32_t>(mv.y) & 7U,
                                 8U, 4U, true,
                                 predU + cPartOffY * cChromaBlockSize, cChromaBlockSize);
                chromaMotionComp(ref, cRefX, cRefY,
                                 static_cast<uint32_t>(mv.x) & 7U,
                                 static_cast<uint32_t>(mv.y) & 7U,
                                 8U, 4U, false,
                                 predV + cPartOffY * cChromaBlockSize, cChromaBlockSize);
            }
        }
        else if (mbTypeRaw == 2U)
        {
            // P_L0_L0_8x16: two 8x16 partitions (left half, right half)
            for (uint32_t p = 0U; p < 2U; ++p)
            {
                const Frame& ref = getRef(refIdxL0[p]);
                MotionVector& mv = mvPart[p];
                uint32_t partOffX = p * 8U;
                int32_t refX = static_cast<int32_t>(mbX * cMbSize + partOffX) + (mv.x >> 2);
                int32_t refY = static_cast<int32_t>(mbY * cMbSize) + (mv.y >> 2);
                lumaMotionComp(ref, refX, refY,
                               static_cast<uint32_t>(mv.x) & 3U,
                               static_cast<uint32_t>(mv.y) & 3U,
                               8U, 16U,
                               predLuma + partOffX, cMbSize);

                uint32_t cPartOffX = p * 4U;
                int32_t cRefX = static_cast<int32_t>(mbX * cChromaBlockSize + cPartOffX) + (mv.x >> 3);
                int32_t cRefY = static_cast<int32_t>(mbY * cChromaBlockSize) + (mv.y >> 3);
                chromaMotionComp(ref, cRefX, cRefY,
                                 static_cast<uint32_t>(mv.x) & 7U,
                                 static_cast<uint32_t>(mv.y) & 7U,
                                 4U, 8U, true,
                                 predU + cPartOffX, cChromaBlockSize);
                chromaMotionComp(ref, cRefX, cRefY,
                                 static_cast<uint32_t>(mv.x) & 7U,
                                 static_cast<uint32_t>(mv.y) & 7U,
                                 4U, 8U, false,
                                 predV + cPartOffX, cChromaBlockSize);
            }
        }
        else
        {
            // P_8x8 / P_8x8ref0: per-8x8 motion compensation — §8.4.2.
            // 4 sub-partitions: TL(0), TR(1), BL(2), BR(3), each 8x8 luma / 4x4 chroma.
            static constexpr uint32_t subLumaOffX[4] = {0, 8, 0, 8};
            static constexpr uint32_t subLumaOffY[4] = {0, 0, 8, 8};
            static constexpr uint32_t subChromaOffX[4] = {0, 4, 0, 4};
            static constexpr uint32_t subChromaOffY[4] = {0, 0, 4, 4};

            for (uint32_t s = 0U; s < 4U; ++s)
            {
                const Frame& ref = getRef(refIdxL0[s]);
                MotionVector& mv = mvSub8x8[s];
                uint32_t lox = subLumaOffX[s], loy = subLumaOffY[s];
                int32_t refX = static_cast<int32_t>(mbX * cMbSize + lox) + (mv.x >> 2);
                int32_t refY = static_cast<int32_t>(mbY * cMbSize + loy) + (mv.y >> 2);
                lumaMotionComp(ref, refX, refY,
                               static_cast<uint32_t>(mv.x) & 3U,
                               static_cast<uint32_t>(mv.y) & 3U,
                               8U, 8U,
                               predLuma + loy * cMbSize + lox, cMbSize);

                uint32_t cox = subChromaOffX[s], coy = subChromaOffY[s];
                int32_t cRefX = static_cast<int32_t>(mbX * cChromaBlockSize + cox) + (mv.x >> 3);
                int32_t cRefY = static_cast<int32_t>(mbY * cChromaBlockSize + coy) + (mv.y >> 3);
                chromaMotionComp(ref, cRefX, cRefY,
                                 static_cast<uint32_t>(mv.x) & 7U,
                                 static_cast<uint32_t>(mv.y) & 7U,
                                 4U, 4U, true,
                                 predU + coy * cChromaBlockSize + cox, cChromaBlockSize);
                chromaMotionComp(ref, cRefX, cRefY,
                                 static_cast<uint32_t>(mv.x) & 7U,
                                 static_cast<uint32_t>(mv.y) & 7U,
                                 4U, 4U, false,
                                 predV + coy * cChromaBlockSize + cox, cChromaBlockSize);
            }
        }

        // Read CBP — ITU-T H.264 §7.3.5.1
        trace_.onMbEnd(mbX, mbY, static_cast<uint32_t>(br.bitOffset())); // pre-CBP bit
        uint32_t cbpCode = br.readUev();
        uint8_t cbp = 0U;
        if (cbpCode < 48U)
            cbp = cCbpTable[cbpCode][1]; // Inter CBP mapping
        uint8_t cbpLuma = cbp & 0x0FU;
        uint8_t cbpChroma = (cbp >> 4U) & 0x03U;
        // Trace CBP: use BlockResidual with blkIdx=99 as CBP marker
        trace_.onBlockResidual(mbX, mbY, 99U, cbpCode, cbpLuma | (cbpChroma << 4U),
                               static_cast<uint32_t>(br.bitOffset()));
        int32_t qp = mbQp;
        if (cbp > 0U)
        {
            int32_t qpDelta = br.readSev();
            qp += qpDelta;
            if (qp < 0) qp += 52;
            if (qp > 51) qp -= 52;
        }

        // Decode luma residual and reconstruct (spec scan order §6.4.3)
        uint32_t yStride = target.yStride();
        for (uint32_t blkIdx = 0U; blkIdx < 16U; ++blkIdx)
        {
            uint32_t blkX = cLuma4x4BlkX[blkIdx];
            uint32_t blkY = cLuma4x4BlkY[blkIdx];
            uint32_t rasterIdx = cLuma4x4ToRaster[blkIdx];

            uint32_t group8x8 = blkIdx >> 2U;
            bool hasResidual = (cbpLuma >> group8x8) & 1U;

            int16_t coeffs[16] = {};
            if (hasResidual)
            {
                int32_t nc = getLumaNc(mbX, mbY, rasterIdx);
                uint32_t bitBefore = static_cast<uint32_t>(br.bitOffset());
                ResidualBlock4x4 resBlock;
                decodeResidualBlock4x4(br, nc, cMaxCoeff4x4, 0U, resBlock);
                uint32_t bitsUsed = static_cast<uint32_t>(br.bitOffset()) - bitBefore;
                trace_.onBlockResidual(mbX, mbY, blkIdx, nc, resBlock.totalCoeff, bitsUsed);
                for (uint32_t i = 0U; i < 16U; ++i)
                    coeffs[i] = resBlock.coeffs[i];
                nnzLuma_[mbIdx * 16U + rasterIdx] = resBlock.totalCoeff;
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
            uint32_t dcBitStart = static_cast<uint32_t>(br.bitOffset());
            ResidualBlock4x4 dcBlockCb, dcBlockCr;
            decodeResidualBlock4x4(br, -1, 4U, 0U, dcBlockCb);
            uint32_t dcCbBits = static_cast<uint32_t>(br.bitOffset()) - dcBitStart;
            trace_.onBlockResidual(mbX, mbY, 16U, -1, dcBlockCb.totalCoeff, dcCbBits);
            uint32_t dcCrStart = static_cast<uint32_t>(br.bitOffset());
            decodeResidualBlock4x4(br, -1, 4U, 0U, dcBlockCr);
            uint32_t dcCrBits = static_cast<uint32_t>(br.bitOffset()) - dcCrStart;
            trace_.onBlockResidual(mbX, mbY, 17U, -1, dcBlockCr.totalCoeff, dcCrBits);
            for (uint32_t i = 0U; i < 4U; ++i) { dcCb[i] = dcBlockCb.coeffs[i]; dcCr[i] = dcBlockCr.coeffs[i]; }
            inverseHadamard2x2(dcCb);
            inverseHadamard2x2(dcCr);

            // Chroma DC dequant — ITU-T H.264 §8.5.12.1
            int32_t cqpDiv6 = chromaQp / 6;
            int32_t cqpMod6 = chromaQp % 6;
            int32_t cScale = cDequantScale[cqpMod6][0];
            for (uint32_t i = 0U; i < 4U; ++i)
            {
                if (dcCb[i] != 0) { int32_t v = dcCb[i] * cScale; dcCb[i] = static_cast<int16_t>(cqpDiv6 >= 1 ? v << (cqpDiv6 - 1) : (v + 1) >> 1); }
                if (dcCr[i] != 0) { int32_t v = dcCr[i] * cScale; dcCr[i] = static_cast<int16_t>(cqpDiv6 >= 1 ? v << (cqpDiv6 - 1) : (v + 1) >> 1); }
            }
        }

        // Stash per-block AC coefficients — decode all Cb first, then Cr (§7.3.5)
        int16_t cbCoeffsBuf[4][16] = {};
        int16_t crCoeffsBuf[4][16] = {};
        for (uint32_t blkIdx = 0U; blkIdx < 4U; ++blkIdx)
        {
            cbCoeffsBuf[blkIdx][0] = dcCb[blkIdx];
            crCoeffsBuf[blkIdx][0] = dcCr[blkIdx];
        }

        if (cbpChroma >= 2U)
        {
            // All Cb AC blocks first — §7.3.5.3
            for (uint32_t blkIdx = 0U; blkIdx < 4U; ++blkIdx)
            {
                int32_t ncCb = getChromaNc(mbX, mbY, blkIdx, true);
                uint32_t acBitBefore = static_cast<uint32_t>(br.bitOffset());
                ResidualBlock4x4 acBlock;
                decodeResidualBlock4x4(br, ncCb, 15U, 1U, acBlock);
                uint32_t acBits = static_cast<uint32_t>(br.bitOffset()) - acBitBefore;
                trace_.onBlockResidual(mbX, mbY, 18U + blkIdx, ncCb, acBlock.totalCoeff, acBits);
                for (uint32_t i = 1U; i < 16U; ++i) cbCoeffsBuf[blkIdx][i] = acBlock.coeffs[i];
                nnzCb_[mbIdx * 4U + blkIdx] = acBlock.totalCoeff;
                int16_t savedDc = cbCoeffsBuf[blkIdx][0];
                inverseQuantize4x4(cbCoeffsBuf[blkIdx], chromaQp);
                cbCoeffsBuf[blkIdx][0] = savedDc;
            }
            // Then all Cr AC blocks — §7.3.5.3
            for (uint32_t blkIdx = 0U; blkIdx < 4U; ++blkIdx)
            {
                int32_t ncCr = getChromaNc(mbX, mbY, blkIdx, false);
                uint32_t acBitBefore = static_cast<uint32_t>(br.bitOffset());
                ResidualBlock4x4 acBlock;
                decodeResidualBlock4x4(br, ncCr, 15U, 1U, acBlock);
                uint32_t acBits = static_cast<uint32_t>(br.bitOffset()) - acBitBefore;
                trace_.onBlockResidual(mbX, mbY, 22U + blkIdx, ncCr, acBlock.totalCoeff, acBits);
                for (uint32_t i = 1U; i < 16U; ++i) crCoeffsBuf[blkIdx][i] = acBlock.coeffs[i];
                nnzCr_[mbIdx * 4U + blkIdx] = acBlock.totalCoeff;
                int16_t savedDc = crCoeffsBuf[blkIdx][0];
                inverseQuantize4x4(crCoeffsBuf[blkIdx], chromaQp);
                crCoeffsBuf[blkIdx][0] = savedDc;
            }
        }

        // Reconstruct chroma using motion-compensated predictions
        uint32_t uvStride = target.uvStride();
        for (uint32_t blkIdx = 0U; blkIdx < 4U; ++blkIdx)
        {
            uint32_t blkX = (blkIdx & 1U) * 4U;
            uint32_t blkY = (blkIdx >> 1U) * 4U;
            inverseDct4x4AddPred(cbCoeffsBuf[blkIdx], predU + blkY * 8U + blkX, 8U,
                                 target.uMb(mbX, mbY) + blkY * uvStride + blkX, uvStride);
            inverseDct4x4AddPred(crCoeffsBuf[blkIdx], predV + blkY * 8U + blkX, 8U,
                                 target.vMb(mbX, mbY) + blkY * uvStride + blkX, uvStride);
        }
        mbQp = qp; // Propagate accumulated QP to next MB
        trace_.onMbEnd(mbX, mbY, static_cast<uint32_t>(br.bitOffset()));
    }

    /** Decode an intra MB within a P-slice (mb_type offset already applied).
     *
     *  NOTE: The I-MB decode functions write to currentFrame_. For P-slices,
     *  the decode target is a separate DPB entry. We copy the decoded MB
     *  from currentFrame_ → target after decode. §7.3.5
     */
    bool decodeIntraMbInPSlice(BitReader& br, const Sps& sps, const Pps& pps,
                                const SliceHeader& sh, int32_t& mbQp,
                                uint32_t mbTypeRaw, Frame& target,
                                uint32_t mbX, uint32_t mbY) noexcept
    {
        (void)sh; (void)target;
        uint32_t mbIdx = mbY * widthInMbs_ + mbX;
        setMbMotion(mbIdx, {0, 0}, -1);

        if (mbTypeRaw == 25U) return true; // I_PCM

        // With activeFrame_ pointing to the DPB target, intra decoders write
        // directly into the correct frame. No neighbourhood copy needed since
        // previous MBs (inter or intra) already wrote their pixels there.
        // No copy-back needed since activeFrame_ IS the target.

        bool ok;
        if (isI16x16(static_cast<uint8_t>(mbTypeRaw)))
            ok = decodeI16x16Mb(br, sps, pps, mbTypeRaw, mbQp, mbX, mbY);
        else
            ok = decodeI4x4Mb(br, sps, pps, mbQp, mbX, mbY);

        return ok;
    }

    // ── CABAC-specific MB decode methods ────────────────────────────────

    /** Decode an intra MB using CABAC.
     *  @param mbQp  [in/out] Accumulated QP — ITU-T H.264 §7.4.5.
     */
    bool decodeCabacIntraMb(BitReader& br, const Sps& sps, const Pps& pps,
                             const SliceHeader& sh, int32_t& mbQp,
                             uint32_t mbX, uint32_t mbY) noexcept
    {
        (void)br; (void)sh;
        uint32_t mbIdx = mbY * widthInMbs_ + mbX;
        setMbMotion(mbIdx, {0, 0}, -1);

        // §9.3.3.1.2: condTermFlagN = 0 when neighbor is I_NxN (I_4x4),
        // = 1 when NOT I_NxN (I_16x16, inter, unavailable).
        bool leftIsI4x4 = (mbX > 0U) && mbIsI4x4_[mbIdx - 1U];
        bool topIsI4x4 = (mbY > 0U) && mbIsI4x4_[mbIdx - widthInMbs_];

        uint32_t mbTypeRaw = cabacDecodeMbTypeI(cabacEngine_, cabacCtx_.data(),
                                                 leftIsI4x4, topIsI4x4);

        // Track I_NxN (I_4x4) for future neighbor context derivation
        mbIsI4x4_[mbIdx] = (mbTypeRaw == 0U);

        if (mbTypeRaw == 25U) return true; // I_PCM
        if (mbTypeRaw == 0U)
        {
            // I_4x4: decode pred modes + residual via CABAC
            return decodeCabacI4x4Mb(sps, pps, mbQp, mbX, mbY);
        }
        else
        {
            // I_16x16: decode using existing path with CABAC residual
            return decodeCabacI16x16Mb(sps, pps, mbTypeRaw, mbQp, mbX, mbY);
        }
    }

    /** Decode I_4x4 MB with CABAC residual. */
    bool decodeCabacI4x4Mb(const Sps& sps, const Pps& pps,
                            int32_t& qp, uint32_t mbX, uint32_t mbY) noexcept
    {
        (void)sps;

        // §7.3.5: For I_NxN with transform_8x8_mode_flag=1, decode
        // transform_size_8x8_flag. If 1 → I_8x8; if 0 → I_4x4.
        bool use8x8Transform = false;
        if (pps.transform8x8Mode_ != 0U)
        {
            use8x8Transform = cabacEngine_.decodeBin(
                cabacCtx_[cCtxTransform8x8]) != 0U;
        }

        // Decode prediction modes: 4 modes for I_8x8, 16 for I_4x4.
        // Both use prev_intra_pred_mode_flag + rem via the same CABAC context
        // (§9.3.3.1.3: prev_intra8x8 uses same ctxIdx 68 as prev_intra4x4).
        // §8.3.1.1 / §8.3.2.1: MPM = min(leftMode, topMode).
        uint8_t predModes[16] = {};
        uint32_t numModeBlocks = use8x8Transform ? 4U : 16U;
        for (uint32_t i = 0U; i < numModeBlocks; ++i)
        {
            // For I_8x8: 4 blocks in 8x8 scan order (TL, TR, BL, BR)
            // For I_4x4: 16 blocks in spec scan order §6.4.3
            uint32_t rasterIdx;
            if (use8x8Transform)
            {
                // 8x8 block i → top-left 4x4 raster index for neighbor lookup
                // Block 0→raster 0, Block 1→raster 2, Block 2→raster 8, Block 3→raster 10
                static constexpr uint32_t c8x8ToRaster[4] = {0U, 2U, 8U, 10U};
                rasterIdx = c8x8ToRaster[i];
            }
            else
            {
                rasterIdx = cLuma4x4ToRaster[i];
            }

            uint8_t leftMode = getNeighborIntra4x4Mode(mbX, mbY, rasterIdx, true, predModes);
            uint8_t topMode  = getNeighborIntra4x4Mode(mbX, mbY, rasterIdx, false, predModes);
            uint8_t mpm = (leftMode < topMode) ? leftMode : topMode;

            uint8_t result = cabacDecodeIntra4x4PredMode(cabacEngine_, cabacCtx_.data());
            uint8_t mode;
            if (result == 0xFFU)
                mode = mpm;
            else
                mode = (result < mpm) ? result : static_cast<uint8_t>(result + 1U);

            if (use8x8Transform)
            {
                // Propagate mode to all 4 constituent 4x4 blocks for neighbor lookup
                static constexpr uint32_t c8x8Sub[4][4] = {
                    {0U, 1U, 4U, 5U}, {2U, 3U, 6U, 7U},
                    {8U, 9U, 12U, 13U}, {10U, 11U, 14U, 15U}
                };
                for (uint32_t s = 0U; s < 4U; ++s)
                    predModes[c8x8Sub[i][s]] = mode;
            }
            else
            {
                predModes[rasterIdx] = mode;
            }
        }

        // Chroma pred mode
        uint32_t mbIdx = mbY * widthInMbs_ + mbX;
        bool leftIntra = (mbX == 0U) || (mbMotion_[(mbIdx - 1U) * 16U + 15U].refIdx == -1);
        bool topIntra = (mbY == 0U) || (mbMotion_[(mbIdx - widthInMbs_) * 16U + 12U].refIdx == -1);
        uint32_t chromaPredMode = cabacDecodeIntraChromaMode(cabacEngine_, cabacCtx_.data(),
                                                              leftIntra, topIntra);

        // CBP via CABAC — §9.3.3.1.1.4 with per-block neighbor CBP
        uint8_t leftCbp = (mbX > 0U) ? mbCbp_[mbIdx - 1U] : 0x2FU;
        uint8_t topCbp = (mbY > 0U) ? mbCbp_[mbIdx - widthInMbs_] : 0x2FU;
        uint8_t cbp = cabacDecodeCbp(cabacEngine_, cabacCtx_.data(),
                                      leftCbp & 0x0FU, topCbp & 0x0FU,
                                      (leftCbp >> 4U) & 3U, (topCbp >> 4U) & 3U);
        uint8_t cbpLuma = cbp & 0x0FU;
        uint8_t cbpChroma = (cbp >> 4U) & 0x03U;
        mbCbp_[mbIdx] = cbp;

        // QP delta
        if (cbp > 0U)
        {
            int32_t qpDelta = cabacDecodeMbQpDelta(cabacEngine_, cabacCtx_.data(), false);
            qp += qpDelta;
            if (qp < 0) qp += 52;
            if (qp > 51) qp -= 52;
        }

        // Decode luma residual — §7.3.5.3
        uint8_t* mbLuma = activeFrame_->yMb(mbX, mbY);
        uint32_t yStride = activeFrame_->yStride();

        if (use8x8Transform)
        {
            // I_8x8: 4 × 8x8 residual blocks — §7.3.5.3 with ctxBlockCat=5
            for (uint32_t blk8 = 0U; blk8 < 4U; ++blk8)
            {
                uint32_t blkX = (blk8 & 1U) * 8U;
                uint32_t blkY = (blk8 >> 1U) * 8U;
                uint32_t absX = mbX * cMbSize + blkX;
                uint32_t absY = mbY * cMbSize + blkY;
                bool hasResidual = (cbpLuma >> blk8) & 1U;

                // 8x8 intra prediction — §8.3.2 with reference sample filtering
                // The prediction mode is stored in predModes[] at the top-left
                // 4x4 sub-block of this 8x8 block.
                static constexpr uint32_t c8x8TopLeft[4] = {0U, 2U, 8U, 10U};
                uint8_t mode8x8 = predModes[c8x8TopLeft[blk8]];
                uint8_t pred8x8[64];
                intraPred8x8Luma(static_cast<Intra4x4Mode>(mode8x8),
                                  *activeFrame_, absX, absY, pred8x8);

                int16_t coeffs[64] = {};
                if (hasResidual)
                {
                    int16_t scanCoeffs[64] = {};
                    cabacDecodeResidual8x8(cabacEngine_, cabacCtx_.data(), scanCoeffs);
                    // Reorder from 8x8 scan to raster via zigzag — §6.4.8
                    for (uint32_t k = 0U; k < 64U; ++k)
                        coeffs[cZigzag8x8[k]] = scanCoeffs[k];
                    inverseQuantize8x8(coeffs, qp);
                }

                // Mark all constituent 4x4 blocks NNZ
                uint32_t base4x4 = (blk8 >> 1U) * 8U + (blk8 & 1U) * 2U;
                for (uint32_t dy = 0U; dy < 2U; ++dy)
                    for (uint32_t dx = 0U; dx < 2U; ++dx)
                        nnzLuma_[mbIdx * 16U + base4x4 + dy * 4U + dx] = hasResidual ? 1U : 0U;

                uint8_t* outPtr = mbLuma + blkY * yStride + blkX;
                inverseDct8x8AddPred(coeffs, pred8x8, 8U, outPtr, yStride);
            }
        }
        else
        {
            // I_4x4: 16 × 4x4 residual blocks (spec scan order §6.4.3)
            for (uint32_t blkIdx = 0U; blkIdx < 16U; ++blkIdx)
            {
                uint32_t blkX = cLuma4x4BlkX[blkIdx];
                uint32_t blkY = cLuma4x4BlkY[blkIdx];
                uint32_t rasterIdx = cLuma4x4ToRaster[blkIdx];
                uint32_t absX = mbX * cMbSize + blkX;
                uint32_t absY = mbY * cMbSize + blkY;

                // Prediction
                uint8_t pred4x4[16];
                uint8_t topBuf[8] = {}, leftBuf[4] = {}, topLeftVal = cDefaultPredValue;
                const uint8_t *top = nullptr, *topRight = nullptr, *left = nullptr, *topLeft = nullptr;
                if (absY > 0U) { const uint8_t* row = activeFrame_->yRow(absY - 1U); for (uint32_t i = 0U; i < 4U; ++i) topBuf[i] = row[absX + i]; top = topBuf; if (absX + 4U < currentFrame_.width() && !cTopRightUnavailScan[blkIdx]) { for (uint32_t i = 0U; i < 4U; ++i) topBuf[4+i] = row[absX+4U+i]; topRight = topBuf+4U; } }
                if (absX > 0U) { for (uint32_t i = 0U; i < 4U; ++i) leftBuf[i] = activeFrame_->y(absX-1U, absY+i); left = leftBuf; }
                if (absX > 0U && absY > 0U) { topLeftVal = activeFrame_->y(absX-1U, absY-1U); topLeft = &topLeftVal; }

                intraPred4x4(static_cast<Intra4x4Mode>(predModes[rasterIdx]), top, topRight, left, topLeft, pred4x4);

                // Residual via CABAC
                uint32_t group8x8 = blkIdx >> 2U;
                bool hasResidual = (cbpLuma >> group8x8) & 1U;

                int16_t coeffs[16] = {};
                if (hasResidual)
                {
                    // §9.3.3.1.1.3: coded_block_flag ctxIdxInc
                    uint32_t leftNnz = 0U, topNnz = 0U;
                    if (rasterIdx % 4U > 0U)
                        leftNnz = nnzLuma_[mbIdx * 16U + rasterIdx - 1U];
                    else if (mbX > 0U)
                        leftNnz = nnzLuma_[(mbIdx - 1U) * 16U + rasterIdx + 3U];
                    if (rasterIdx >= 4U)
                        topNnz = nnzLuma_[mbIdx * 16U + rasterIdx - 4U];
                    else if (mbY > 0U)
                        topNnz = nnzLuma_[(mbIdx - widthInMbs_) * 16U + rasterIdx + 12U];
                    uint32_t cbfCtxInc = (leftNnz != 0U ? 1U : 0U) + (topNnz != 0U ? 2U : 0U);

                    int16_t scanCoeffs[16] = {};
                    uint32_t numNonZero = cabacDecodeResidual4x4(cabacEngine_, cabacCtx_.data(),
                                                                  scanCoeffs, 16U, 2U, cbfCtxInc);
                    for (uint32_t k = 0U; k < 16U; ++k)
                        coeffs[cZigzag4x4[k]] = scanCoeffs[k];
                    nnzLuma_[mbIdx * 16U + rasterIdx] = (numNonZero > 0U) ? 1U : 0U;
                    inverseQuantize4x4(coeffs, qp);
                }
                else
                {
                    nnzLuma_[mbIdx * 16U + rasterIdx] = 0U;
                }

                uint8_t* outPtr = mbLuma + blkY * yStride + blkX;
                inverseDct4x4AddPred(coeffs, pred4x4, 4U, outPtr, yStride);
            }
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
        mbCbp_[mbIdx] = cbpLuma | (cbpChroma << 4U); // Store for neighbor context
        bool leftIntra = (mbX == 0U) || (mbMotion_[(mbIdx - 1U) * 16U + 15U].refIdx == -1);
        bool topIntra = (mbY == 0U) || (mbMotion_[(mbIdx - widthInMbs_) * 16U + 12U].refIdx == -1);
        uint32_t chromaPredMode = cabacDecodeIntraChromaMode(cabacEngine_, cabacCtx_.data(),
                                                              leftIntra, topIntra);

        // QP delta
        int32_t qpDelta = cabacDecodeMbQpDelta(cabacEngine_, cabacCtx_.data(), false);
        qp += qpDelta;
        qp = ((qp % 52) + 52) % 52; // Proper modular wrapping for any delta

        // Generate 16x16 prediction
        uint8_t lumaPred[256];
        intraPred16x16(static_cast<Intra16x16Mode>(predMode), *activeFrame_, mbX, mbY, lumaPred);

        // Decode DC block via CABAC (category 0 = Luma DC)
        // CABAC outputs in scan order; reorder to raster for Hadamard.
        int16_t dcScan[16] = {};
        cabacDecodeResidual4x4(cabacEngine_, cabacCtx_.data(), dcScan, 16U, 0U);

        int16_t dcCoeffs[16] = {};
        for (uint32_t i = 0U; i < 16U; ++i)
            dcCoeffs[cZigzag4x4[i]] = dcScan[i];

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

        // Decode AC blocks (spec scan order §6.4.3)
        uint8_t* mbLuma = activeFrame_->yMb(mbX, mbY);
        uint32_t yStride = activeFrame_->yStride();

        for (uint32_t blkIdx = 0U; blkIdx < 16U; ++blkIdx)
        {
            uint32_t blkX = cLuma4x4BlkX[blkIdx];
            uint32_t blkY = cLuma4x4BlkY[blkIdx];
            uint32_t rasterIdx = cLuma4x4ToRaster[blkIdx];

            int16_t coeffs[16] = {};
            // Hadamard output is in raster order — DC already dequantized above
            coeffs[0] = dcCoeffs[rasterIdx];

            if (cbpLuma)
            {
                // §9.3.3.1.1.3: coded_block_flag ctxIdxInc = condTermFlagA + 2*condTermFlagB
                uint32_t leftNnz = 0U, topNnz = 0U;
                if (rasterIdx % 4U > 0U)
                    leftNnz = nnzLuma_[mbIdx * 16U + rasterIdx - 1U];
                else if (mbX > 0U)
                    leftNnz = nnzLuma_[(mbIdx - 1U) * 16U + rasterIdx + 3U];
                if (rasterIdx >= 4U)
                    topNnz = nnzLuma_[mbIdx * 16U + rasterIdx - 4U];
                else if (mbY > 0U)
                    topNnz = nnzLuma_[(mbIdx - widthInMbs_) * 16U + rasterIdx + 12U];
                uint32_t cbfInc = (leftNnz != 0U ? 1U : 0U) + (topNnz != 0U ? 2U : 0U);

                int16_t acScan[16] = {};
                uint32_t numSig = cabacDecodeResidual4x4(cabacEngine_, cabacCtx_.data(),
                                                          acScan, 15U, 1U, cbfInc);
                // AC coefficients in scan order 0..14 → raster positions via zigzag.
                // Zigzag indices 1..15 correspond to AC positions (skip DC at index 0).
                for (uint32_t i = 0U; i < 15U; ++i)
                    coeffs[cZigzag4x4[i + 1U]] = acScan[i];
                nnzLuma_[mbIdx * 16U + rasterIdx] = (numSig > 0U) ? 1U : 0U;
            }

            // Save DC (already dequantized via Hadamard path), dequant AC, restore DC.
            // §8.5.12.1: DC was scaled above; inverseQuantize4x4 would re-dequant it.
            int16_t savedDc = coeffs[0];
            inverseQuantize4x4(coeffs, qp);
            coeffs[0] = savedDc;
            uint8_t* predPtr = lumaPred + blkY * 16U + blkX;
            uint8_t* outPtr = mbLuma + blkY * yStride + blkX;
            inverseDct4x4AddPred(coeffs, predPtr, 16U, outPtr, yStride);
        }

        decodeChromaCabac(pps, chromaPredMode, cbpChroma, qp, mbX, mbY);
        return true;
    }

    /** Decode P-inter MB with CABAC entropy.
     *  @param mbQp  [in/out] Accumulated QP — ITU-T H.264 §7.4.5.
     */
    void decodeCabacPInterMb(BitReader& br, const Sps& sps, const Pps& pps,
                              int32_t& mbQp, uint32_t mbTypeRaw,
                              Frame& target, const Frame& ref,
                              uint32_t mbX, uint32_t mbY) noexcept
    {
        (void)br; (void)sps; (void)mbTypeRaw;
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
        setMbMotion(mbIdx, mv, 0);

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

        // CBP via CABAC — ��9.3.3.1.1.4 with per-block neighbor CBP
        uint8_t cbp;
        {
            uint8_t lCbp = (mbX > 0U) ? mbCbp_[mbIdx - 1U] : 0x2FU;
            uint8_t tCbp = (mbY > 0U) ? mbCbp_[mbIdx - widthInMbs_] : 0x2FU;
            cbp = cabacDecodeCbp(cabacEngine_, cabacCtx_.data(),
                                 lCbp & 0x0FU, tCbp & 0x0FU,
                                 (lCbp >> 4U) & 3U, (tCbp >> 4U) & 3U);
        }
        uint8_t cbpLuma = cbp & 0x0FU;
        uint8_t cbpChroma = (cbp >> 4U) & 0x03U;
        mbCbp_[mbIdx] = cbp;

        // §7.3.5: For inter MBs with transform_8x8_mode_flag AND cbp != 0,
        // decode transform_size_8x8_flag after CBP.
        if (cbp != 0U && pps.transform8x8Mode_ != 0U)
        {
            /* bool use8x8 = */ cabacEngine_.decodeBin(cabacCtx_[cCtxTransform8x8]);
            // TODO: use 8x8 inverse transform when flag is set
        }

        int32_t qp = mbQp;
        if (cbp > 0U)
        {
            int32_t qpDelta = cabacDecodeMbQpDelta(cabacEngine_, cabacCtx_.data(), false);
            qp += qpDelta;
            if (qp < 0) qp += 52;
            if (qp > 51) qp -= 52;
        }
        mbQp = qp; // Propagate accumulated QP

        // Luma residual via CABAC (spec scan order §6.4.3)
        uint32_t yStride = target.yStride();
        for (uint32_t blkIdx = 0U; blkIdx < 16U; ++blkIdx)
        {
            uint32_t blkX = cLuma4x4BlkX[blkIdx];
            uint32_t blkY = cLuma4x4BlkY[blkIdx];
            uint32_t rasterIdx = cLuma4x4ToRaster[blkIdx];
            uint32_t group8x8 = blkIdx >> 2U;

            int16_t coeffs[16] = {};
            if ((cbpLuma >> group8x8) & 1U)
            {
                cabacDecodeResidual4x4(cabacEngine_, cabacCtx_.data(), coeffs, 16U, 2U);
                nnzLuma_[mbIdx * 16U + rasterIdx] = 1U;
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
                // Save DC (already dequantized via Hadamard), dequant AC, restore DC.
                // §8.5.12.1: DC was scaled in the Hadamard dequant above.
                int16_t savedDcCb = cbCoeffs[0];
                inverseQuantize4x4(cbCoeffs, chromaQp);
                cbCoeffs[0] = savedDcCb;
                cabacDecodeResidual4x4(cabacEngine_, cabacCtx_.data(), acCr, 15U, 4U);
                for (uint32_t i = 1U; i < 16U; ++i) crCoeffs[i] = acCr[i - 1U];
                int16_t savedDcCr = crCoeffs[0];
                inverseQuantize4x4(crCoeffs, chromaQp);
                crCoeffs[0] = savedDcCr;
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
        intraPredChroma8x8(chromaMode, *activeFrame_, mbX, mbY, true, predU);
        intraPredChroma8x8(chromaMode, *activeFrame_, mbX, mbY, false, predV);

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

        uint8_t* mbU = activeFrame_->uMb(mbX, mbY);
        uint8_t* mbV = activeFrame_->vMb(mbX, mbY);
        uint32_t uvStride = activeFrame_->uvStride();
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
                // Save DC (already dequantized via Hadamard), dequant AC, restore DC.
                // §8.5.12.1: DC was scaled in the Hadamard dequant above.
                int16_t savedDcCb = cbCoeffs[0];
                inverseQuantize4x4(cbCoeffs, chromaQp);
                cbCoeffs[0] = savedDcCb;
                cabacDecodeResidual4x4(cabacEngine_, cabacCtx_.data(), acCr, 15U, 4U);
                for (uint32_t i = 1U; i < 16U; ++i) crCoeffs[i] = acCr[i - 1U];
                int16_t savedDcCr = crCoeffs[0];
                inverseQuantize4x4(crCoeffs, chromaQp);
                crCoeffs[0] = savedDcCr;
            }

            inverseDct4x4AddPred(cbCoeffs, predU + blkY * 8U + blkX, 8U, mbU + blkY * uvStride + blkX, uvStride);
            inverseDct4x4AddPred(crCoeffs, predV + blkY * 8U + blkX, 8U, mbV + blkY * uvStride + blkX, uvStride);
        }
    }
};

} // namespace sub0h264

#endif // CROG_SUB0H264_DECODER_HPP
