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
#include "cavlc.hpp"
#include "frame.hpp"
#include "intra_pred.hpp"
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
        return currentFrame_.isAllocated() ? &currentFrame_ : nullptr;
    }

    /** @return Mutable reference to parameter sets. */
    ParamSets& paramSets() noexcept { return paramSets_; }

    /** @return Number of frames decoded so far. */
    uint32_t frameCount() const noexcept { return frameCount_; }

private:
    ParamSets paramSets_;
    Frame currentFrame_;
    uint32_t frameCount_ = 0U;

    // Per-frame MB context: non-zero coefficient counts for CAVLC context
    std::vector<uint8_t> nnzLuma_;    // [mbIdx * 16 + blkIdx]
    std::vector<uint8_t> nnzCb_;      // [mbIdx * 4 + blkIdx]
    std::vector<uint8_t> nnzCr_;      // [mbIdx * 4 + blkIdx]
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

        // Allocate frame if needed
        if (!currentFrame_.isAllocated() ||
            currentFrame_.width() != sps->width() ||
            currentFrame_.height() != sps->height())
        {
            currentFrame_.allocate(sps->width(), sps->height());
            widthInMbs_ = sps->widthInMbs_;
            heightInMbs_ = sps->heightInMbs_;
            uint32_t totalMbs = static_cast<uint32_t>(widthInMbs_) * heightInMbs_;
            nnzLuma_.resize(totalMbs * 16U, 0U);
            nnzCb_.resize(totalMbs * 4U, 0U);
            nnzCr_.resize(totalMbs * 4U, 0U);
        }

        // Clear NNZ context for new frame (IDR resets everything)
        if (isIdr)
        {
            std::fill(nnzLuma_.begin(), nnzLuma_.end(), 0U);
            std::fill(nnzCb_.begin(), nnzCb_.end(), 0U);
            std::fill(nnzCr_.begin(), nnzCr_.end(), 0U);
        }

        // Compute effective QP
        int32_t sliceQp = pps->picInitQp_ + sh.sliceQpDelta_;

        // Only decode I-slices for now
        if (sh.sliceType_ != SliceType::I)
            return DecodeStatus::NeedMoreData;

        // Decode macroblocks
        for (uint32_t mbAddr = sh.firstMbInSlice_; mbAddr < sps->totalMbs(); ++mbAddr)
        {
            uint32_t mbX = mbAddr % widthInMbs_;
            uint32_t mbY = mbAddr / widthInMbs_;

            if (!decodeIntraMb(br, *sps, *pps, sh, sliceQp, mbX, mbY))
                break; // Slice ended or error
        }

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
        uint32_t chromaPredMode = br.readUev();

        // QP delta
        int32_t qpDelta = br.readSev();
        qp += qpDelta;
        if (qp < 0) qp += 52;
        if (qp > 51) qp -= 52;

        // 1. Generate 16x16 luma prediction
        uint8_t lumaPred[256];
        intraPred16x16(static_cast<Intra16x16Mode>(predMode),
                       currentFrame_, mbX, mbY, lumaPred);

        // 2. Decode luma DC block (4x4 Hadamard)
        int32_t dcNc = getLumaNc(mbX, mbY, 0U);
        ResidualBlock4x4 dcBlock;
        decodeResidualBlock4x4(br, dcNc, cMaxCoeff4x4, 0U, dcBlock);

        int16_t dcCoeffs[16];
        for (uint32_t i = 0U; i < 16U; ++i)
            dcCoeffs[i] = dcBlock.coeffs[i];

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

            // Inverse quantize AC coefficients
            inverseQuantize4x4(coeffs, qp);

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
    bool decodeI4x4Mb(BitReader& br, const Sps& sps, const Pps& pps,
                       int32_t& qp, uint32_t mbX, uint32_t mbY) noexcept
    {
        // Read intra 4x4 prediction modes for all 16 blocks
        uint8_t predModes[16] = {};
        for (uint32_t i = 0U; i < 16U; ++i)
        {
            uint32_t prevIntraPredModeFlag = br.readBit();
            if (prevIntraPredModeFlag)
            {
                // Use most probable mode (simplified: use DC=2)
                predModes[i] = 2U; // DC as default
            }
            else
            {
                predModes[i] = static_cast<uint8_t>(br.readBits(3U));
            }
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

        // QP delta (only if CBP > 0)
        if (cbp > 0U)
        {
            int32_t qpDelta = br.readSev();
            qp += qpDelta;
            if (qp < 0) qp += 52;
            if (qp > 51) qp -= 52;
        }

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

            // Reconstruct: inverse DCT + prediction + clip
            uint8_t* outPtr = mbLuma + blkY * yStride + blkX;
            inverseDct4x4AddPred(coeffs, pred4x4, 4U, outPtr, yStride);
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
        auto chromaMode = static_cast<IntraChromaMode>(chromaPredMode);

        // Generate chroma predictions (8x8 for each plane)
        uint8_t predU[64], predV[64];
        intraPredChroma8x8(chromaMode, currentFrame_, mbX, mbY, true, predU);
        intraPredChroma8x8(chromaMode, currentFrame_, mbX, mbY, false, predV);

        // Chroma QP
        int32_t chromaQpIdx = qp + pps.chromaQpIndexOffset_;
        chromaQpIdx = std::max(0, std::min(51, chromaQpIdx));
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
};

} // namespace sub0h264

#endif // CROG_SUB0H264_DECODER_HPP
