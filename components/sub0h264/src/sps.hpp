/** Sub0h264 — Sequence Parameter Set (SPS) parser
 *
 *  Parses H.264 SPS NAL units. Reference: ITU-T H.264 §7.3.2.1
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef CROG_SUB0H264_SPS_HPP
#define CROG_SUB0H264_SPS_HPP

#include "bitstream.hpp"
#include "sub0h264/sub0h264_types.hpp"

#include <cstdint>
#include <cstring>

namespace sub0h264 {

/** Maximum supported SPS count (spec allows 0-31). */
inline constexpr uint32_t cMaxSpsCount = 32U;

/** Maximum reference frames in DPB. */
inline constexpr uint32_t cMaxRefFrames = 16U;

/** Maximum supported width/height in pixels (our constraint). */
inline constexpr uint32_t cMaxWidth  = 640U;
inline constexpr uint32_t cMaxHeight = 480U;

/** H.264 profile IDCs. */
inline constexpr uint8_t cProfileBaseline = 66U;
inline constexpr uint8_t cProfileMain     = 77U;
inline constexpr uint8_t cProfileExtended = 88U;
inline constexpr uint8_t cProfileHigh     = 100U;

/** Scaling list (High profile). */
struct ScalingList4x4
{
    int16_t data_[16]{};
    bool useDefault_ = false;
};

struct ScalingList8x8
{
    int16_t data_[64]{};
    bool useDefault_ = false;
};

/** Parsed Sequence Parameter Set. */
struct Sps
{
    bool valid_ = false;

    // Identity
    uint8_t spsId_ = 0U;
    uint8_t profileIdc_ = 0U;
    uint8_t levelIdc_ = 0U;
    uint8_t constraintSet0_ = 0U;
    uint8_t constraintSet1_ = 0U;

    // Frame dimensions (in macroblocks)
    uint16_t widthInMbs_ = 0U;   ///< pic_width_in_mbs_minus1 + 1
    uint16_t heightInMbs_ = 0U;  ///< pic_height_in_map_units_minus1 + 1

    // Derived pixel dimensions
    uint16_t width() const noexcept { return widthInMbs_ * 16U; }
    uint16_t height() const noexcept { return heightInMbs_ * 16U; }
    uint32_t totalMbs() const noexcept { return static_cast<uint32_t>(widthInMbs_) * heightInMbs_; }

    // Frame numbering
    uint8_t bitsInFrameNum_ = 0U;          ///< log2_max_frame_num_minus4 + 4
    uint32_t maxFrameNum() const noexcept { return 1U << bitsInFrameNum_; }

    // Picture order count
    uint8_t picOrderCntType_ = 0U;         ///< [0-2]
    uint8_t log2MaxPicOrderCntLsb_ = 0U;   ///< log2_max_pic_order_cnt_lsb_minus4 + 4 (type 0)
    int32_t maxPicOrderCntLsb() const noexcept { return 1 << log2MaxPicOrderCntLsb_; }
    // POC type 1 fields
    uint8_t deltaPicOrderAlwaysZero_ = 0U;
    int32_t offsetForNonRefPic_ = 0;
    int32_t offsetForTopToBottomField_ = 0;
    uint8_t numRefFramesInPocCycle_ = 0U;
    int32_t offsetForRefFrame_[255]{};

    // Reference frames
    uint8_t numRefFrames_ = 0U;            ///< max_num_ref_frames
    uint8_t gapsInFrameNumAllowed_ = 0U;

    // Flags
    uint8_t frameMbsOnly_ = 1U;            ///< 1=progressive only (we require this)
    uint8_t mbAdaptiveFrameField_ = 0U;
    uint8_t direct8x8Inference_ = 0U;

    // Cropping
    bool frameCropping_ = false;
    uint16_t cropLeft_ = 0U;
    uint16_t cropRight_ = 0U;
    uint16_t cropTop_ = 0U;
    uint16_t cropBottom_ = 0U;

    uint16_t croppedWidth() const noexcept
    {
        return frameCropping_ ? width() - (cropLeft_ + cropRight_) * 2U : width();
    }
    uint16_t croppedHeight() const noexcept
    {
        return frameCropping_ ? height() - (cropTop_ + cropBottom_) * 2U : height();
    }

    // High profile extensions
    int32_t chromaFormatIdc_ = 1;          ///< 1 = 4:2:0 (only supported)
    int32_t bitDepthLuma_ = 8;
    int32_t bitDepthChroma_ = 8;
    bool seqScalingMatrixPresent_ = false;
    ScalingList4x4 scalingList4x4_[6]{};
    ScalingList8x8 scalingList8x8_[2]{};

    // VUI
    bool vuiPresent_ = false;
};

/** Parse a scaling list from the bitstream.
 *  Reference: ITU-T H.264 §7.3.2.1.1.1
 */
inline Result parseScalingList(BitReader& br, int16_t* list, uint32_t size,
                               bool& useDefault) noexcept
{
    int32_t lastScale = 8;
    int32_t nextScale = 8;

    for (uint32_t j = 0U; j < size; ++j)
    {
        if (nextScale != 0)
        {
            int32_t deltaScale = br.readSev();
            nextScale = (lastScale + deltaScale + 256) % 256;
            useDefault = (j == 0U && nextScale == 0);
        }
        list[j] = static_cast<int16_t>((nextScale == 0) ? lastScale : nextScale);
        lastScale = list[j];
    }
    return Result::Ok;
}

/** Parse an SPS NAL unit.
 *
 *  @param br   BitReader positioned at the first byte of RBSP (after NAL header)
 *  @param[out] sps  Parsed SPS
 *  @return Result::Ok on success
 */
inline Result parseSps(BitReader& br, Sps& sps) noexcept
{
    std::memset(&sps, 0, sizeof(Sps));

    // profile_idc (8 bits)
    sps.profileIdc_ = static_cast<uint8_t>(br.readBits(8U));

    // constraint_set flags + reserved
    sps.constraintSet0_ = static_cast<uint8_t>(br.readBit());
    sps.constraintSet1_ = static_cast<uint8_t>(br.readBit());
    br.skipBits(6U); // constraint_set2..3 + reserved_zero_4bits

    // level_idc (8 bits)
    sps.levelIdc_ = static_cast<uint8_t>(br.readBits(8U));

    // seq_parameter_set_id
    sps.spsId_ = static_cast<uint8_t>(br.readUev());
    if (sps.spsId_ >= cMaxSpsCount)
        return Result::ErrorInvalidParam;

    // Profile validation
    bool isBaseline = (sps.profileIdc_ == cProfileBaseline);
    bool isMain     = (sps.profileIdc_ == cProfileMain);
    bool isHigh     = (sps.profileIdc_ == cProfileHigh);
    bool isExtended = (sps.profileIdc_ == cProfileExtended);

    if (!isBaseline && !isMain && !isHigh)
    {
        if (!isExtended || (sps.constraintSet0_ == 0U && sps.constraintSet1_ == 0U))
            return Result::ErrorUnsupported;
    }

    // High profile extensions
    sps.chromaFormatIdc_ = 1;
    sps.bitDepthLuma_ = 8;
    sps.bitDepthChroma_ = 8;

    if (isHigh)
    {
        sps.chromaFormatIdc_ = static_cast<int32_t>(br.readUev());
        if (sps.chromaFormatIdc_ != 1) // Only 4:2:0 supported
            return Result::ErrorUnsupported;

        sps.bitDepthLuma_ = 8 + static_cast<int32_t>(br.readUev());
        sps.bitDepthChroma_ = 8 + static_cast<int32_t>(br.readUev());
        if (sps.bitDepthLuma_ != 8 || sps.bitDepthChroma_ != 8)
            return Result::ErrorUnsupported;

        br.readBit(); // qpprime_y_zero_transform_bypass_flag

        sps.seqScalingMatrixPresent_ = br.readBit() != 0U;
        if (sps.seqScalingMatrixPresent_)
        {
            for (uint32_t i = 0U; i < 8U; ++i)
            {
                bool present = br.readBit() != 0U;
                if (present)
                {
                    if (i < 6U)
                        parseScalingList(br, sps.scalingList4x4_[i].data_, 16U,
                                         sps.scalingList4x4_[i].useDefault_);
                    else
                        parseScalingList(br, sps.scalingList8x8_[i - 6U].data_, 64U,
                                         sps.scalingList8x8_[i - 6U].useDefault_);
                }
            }
        }
    }

    // log2_max_frame_num_minus4
    sps.bitsInFrameNum_ = static_cast<uint8_t>(br.readUev() + 4U);
    if (sps.bitsInFrameNum_ > 16U)
        return Result::ErrorInvalidParam;

    // pic_order_cnt_type
    sps.picOrderCntType_ = static_cast<uint8_t>(br.readUev());
    if (sps.picOrderCntType_ > 2U)
        return Result::ErrorInvalidParam;

    if (sps.picOrderCntType_ == 0U)
    {
        sps.log2MaxPicOrderCntLsb_ = static_cast<uint8_t>(br.readUev() + 4U);
        if (sps.log2MaxPicOrderCntLsb_ > 16U)
            return Result::ErrorInvalidParam;
    }
    else if (sps.picOrderCntType_ == 1U)
    {
        sps.deltaPicOrderAlwaysZero_ = static_cast<uint8_t>(br.readBit());
        sps.offsetForNonRefPic_ = br.readSev();
        sps.offsetForTopToBottomField_ = br.readSev();
        sps.numRefFramesInPocCycle_ = static_cast<uint8_t>(br.readUev());
        for (uint32_t i = 0U; i < sps.numRefFramesInPocCycle_; ++i)
            sps.offsetForRefFrame_[i] = br.readSev();
    }

    // max_num_ref_frames
    sps.numRefFrames_ = static_cast<uint8_t>(br.readUev());
    if (sps.numRefFrames_ > cMaxRefFrames)
        return Result::ErrorInvalidParam;

    // gaps_in_frame_num_value_allowed_flag
    sps.gapsInFrameNumAllowed_ = static_cast<uint8_t>(br.readBit());

    // pic_width_in_mbs_minus1
    sps.widthInMbs_ = static_cast<uint16_t>(br.readUev() + 1U);

    // pic_height_in_map_units_minus1
    sps.heightInMbs_ = static_cast<uint16_t>(br.readUev() + 1U);

    // Resolution check
    if (sps.width() > cMaxWidth || sps.height() > cMaxHeight)
        return Result::ErrorUnsupported;

    // frame_mbs_only_flag
    sps.frameMbsOnly_ = static_cast<uint8_t>(br.readBit());
    if (!sps.frameMbsOnly_)
    {
        sps.mbAdaptiveFrameField_ = static_cast<uint8_t>(br.readBit());
        // We only support progressive (frame_mbs_only=1) for simplicity
        // but parse the field anyway for compatibility
    }

    // direct_8x8_inference_flag
    sps.direct8x8Inference_ = static_cast<uint8_t>(br.readBit());

    // frame_cropping_flag
    sps.frameCropping_ = br.readBit() != 0U;
    if (sps.frameCropping_)
    {
        sps.cropLeft_   = static_cast<uint16_t>(br.readUev());
        sps.cropRight_  = static_cast<uint16_t>(br.readUev());
        sps.cropTop_    = static_cast<uint16_t>(br.readUev());
        sps.cropBottom_ = static_cast<uint16_t>(br.readUev());
    }

    // vui_parameters_present_flag
    sps.vuiPresent_ = br.readBit() != 0U;
    // VUI parsing deferred — not critical for decode correctness

    sps.valid_ = true;
    return Result::Ok;
}

} // namespace sub0h264

#endif // CROG_SUB0H264_SPS_HPP
