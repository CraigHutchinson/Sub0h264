/** Sub0h264 — Slice header parser
 *
 *  Parses H.264 slice headers. Reference: ITU-T H.264 §7.3.3
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef CROG_SUB0H264_SLICE_HPP
#define CROG_SUB0H264_SLICE_HPP

#include "bitstream.hpp"
#include "sps.hpp"
#include "pps.hpp"
#include "sub0h264/sub0h264_types.hpp"

#include <cstdint>

namespace sub0h264 {

/** H.264 slice types. Values 5-9 map to 0-4 (all slices in picture are same type). */
/// H.264 slice types — ITU-T H.264 Table 7-6.
enum class SliceType : uint8_t
{
    P  = 0U,
    B  = 1U,
    I  = 2U,
    SP = 3U,
    SI = 4U,
};

/** Decoded reference picture marking operation (for IDR and non-IDR). */
struct DecRefPicMarking
{
    bool noOutputOfPriorPics_ = false;
    bool longTermReference_ = false;
    bool adaptiveRefPicMarking_ = false;
    // Full MMCO commands deferred to Phase 6 (DPB management)
};

/** Parsed slice header. */
struct SliceHeader
{
    // Core fields
    uint32_t firstMbInSlice_ = 0U;
    SliceType sliceType_ = SliceType::I;
    uint8_t ppsId_ = 0U;
    uint16_t frameNum_ = 0U;

    // IDR-specific
    bool isIdr_ = false;
    uint32_t idrPicId_ = 0U;

    // Picture order count (POC)
    int32_t picOrderCntLsb_ = 0;
    int32_t deltaPicOrderCntBottom_ = 0;
    int32_t deltaPicOrderCnt_[2] = {};

    // Field-specific (we support frame-only, but parse for compatibility)
    bool fieldPicFlag_ = false;
    bool bottomFieldFlag_ = false;

    // Redundant picture count
    uint8_t redundantPicCnt_ = 0U;

    // Reference picture list modification
    bool refPicListModificationL0_ = false;
    bool refPicListModificationL1_ = false;

    // QP
    int32_t sliceQpDelta_ = 0;
    int32_t sliceQp() const noexcept { return cDefaultPicInitQp + sliceQpDelta_; } // Needs pps.picInitQp_

    // Deblocking filter
    uint8_t disableDeblockingFilter_ = 0U;   ///< 0=enabled, 1=disabled, 2=disabled at slice boundary
    int32_t sliceAlphaC0Offset_ = 0;         ///< alpha offset / 2
    int32_t sliceBetaOffset_ = 0;            ///< beta offset / 2

    // CABAC
    uint8_t cabacInitIdc_ = 0U;              ///< 0-2 (only for CABAC slices)

    // B-slice specific
    bool directSpatialMvPred_ = false;
    uint8_t numRefIdxActiveL0_ = 0U;
    uint8_t numRefIdxActiveL1_ = 0U;

    // Dec ref pic marking
    DecRefPicMarking decRefPicMarking_;
};

/** Skip reference picture list modification syntax.
 *  Full implementation deferred to Phase 6.
 */
inline void skipRefPicListModification(BitReader& br, SliceType sliceType) noexcept
{
    // ref_pic_list_modification for L0 (P, B, SP slices)
    if (sliceType != SliceType::I && sliceType != SliceType::SI)
    {
        bool modFlag = br.readBit() != 0U;
        if (modFlag)
        {
            uint32_t op;
            do {
                op = br.readUev();
                if (op == 0U || op == 1U)
                    br.readUev(); // abs_diff_pic_num_minus1 or long_term_pic_num
                else if (op == 2U)
                    br.readUev(); // long_term_pic_num
            } while (op != 3U && !br.isExhausted());
        }
    }

    // ref_pic_list_modification for L1 (B slices)
    if (sliceType == SliceType::B)
    {
        bool modFlag = br.readBit() != 0U;
        if (modFlag)
        {
            uint32_t op;
            do {
                op = br.readUev();
                if (op == 0U || op == 1U)
                    br.readUev();
                else if (op == 2U)
                    br.readUev();
            } while (op != 3U && !br.isExhausted());
        }
    }
}

/** Skip dec_ref_pic_marking syntax.
 *  Full implementation deferred to Phase 6.
 */
inline void skipDecRefPicMarking(BitReader& br, bool isIdr,
                                  DecRefPicMarking& marking) noexcept
{
    if (isIdr)
    {
        marking.noOutputOfPriorPics_ = br.readBit() != 0U;
        marking.longTermReference_ = br.readBit() != 0U;
    }
    else
    {
        marking.adaptiveRefPicMarking_ = br.readBit() != 0U;
        if (marking.adaptiveRefPicMarking_)
        {
            uint32_t op;
            do {
                op = br.readUev();
                if (op == 1U || op == 3U)
                    br.readUev(); // difference_of_pic_nums_minus1
                if (op == 2U)
                    br.readUev(); // long_term_pic_num
                if (op == 3U || op == 6U)
                    br.readUev(); // long_term_frame_idx
                if (op == 4U)
                    br.readUev(); // max_long_term_frame_idx_plus1
            } while (op != 0U && !br.isExhausted());
        }
    }
}

/** Parse a slice header from RBSP data.
 *
 *  @param br     BitReader positioned at start of slice header RBSP
 *  @param sps    Active SPS (resolved via PPS)
 *  @param pps    Active PPS
 *  @param isIdr  True if this NAL is an IDR slice (type 5)
 *  @param nalRefIdc  nal_ref_idc from NAL header
 *  @param[out] sh  Parsed slice header
 *  @return Result::Ok on success
 */
inline Result parseSliceHeader(BitReader& br, const Sps& sps, const Pps& pps,
                               bool isIdr, uint8_t nalRefIdc,
                               SliceHeader& sh) noexcept
{
    sh = SliceHeader{};
    sh.isIdr_ = isIdr;

    // first_mb_in_slice
    sh.firstMbInSlice_ = br.readUev();
    if (sh.firstMbInSlice_ >= sps.totalMbs())
        return Result::ErrorInvalidParam;

    // slice_type (0-9, values 5-9 mean "all slices in picture are this type")
    uint32_t sliceTypeRaw = br.readUev();
    if (sliceTypeRaw > 9U)
        return Result::ErrorInvalidParam;
    if (sliceTypeRaw >= 5U)
        sliceTypeRaw -= 5U;
    sh.sliceType_ = static_cast<SliceType>(sliceTypeRaw);

    // pic_parameter_set_id
    sh.ppsId_ = static_cast<uint8_t>(br.readUev());

    // frame_num
    sh.frameNum_ = static_cast<uint16_t>(br.readBits(sps.bitsInFrameNum_));

    // field_pic_flag (only if not frame_mbs_only)
    if (!sps.frameMbsOnly_)
    {
        sh.fieldPicFlag_ = br.readBit() != 0U;
        if (sh.fieldPicFlag_)
            sh.bottomFieldFlag_ = br.readBit() != 0U;
    }

    // idr_pic_id (only for IDR slices)
    if (isIdr)
    {
        sh.idrPicId_ = br.readUev();
        if (sh.idrPicId_ > 65535U)
            return Result::ErrorInvalidParam;
    }

    // Picture order count
    if (sps.picOrderCntType_ == 0U)
    {
        sh.picOrderCntLsb_ = static_cast<int32_t>(br.readBits(sps.log2MaxPicOrderCntLsb_));

        if (pps.picOrderPresent_ && !sh.fieldPicFlag_)
            sh.deltaPicOrderCntBottom_ = br.readSev();
    }

    if (sps.picOrderCntType_ == 1U && !sps.deltaPicOrderAlwaysZero_)
    {
        sh.deltaPicOrderCnt_[0] = br.readSev();
        if (pps.picOrderPresent_ && !sh.fieldPicFlag_)
            sh.deltaPicOrderCnt_[1] = br.readSev();
    }

    // redundant_pic_cnt
    if (pps.redundantPicCntPresent_)
        sh.redundantPicCnt_ = static_cast<uint8_t>(br.readUev());

    // direct_spatial_mv_pred_flag (B slices)
    if (sh.sliceType_ == SliceType::B)
        sh.directSpatialMvPred_ = br.readBit() != 0U;

    // num_ref_idx_active_override_flag
    sh.numRefIdxActiveL0_ = pps.numRefIdxL0Active_;
    sh.numRefIdxActiveL1_ = pps.numRefIdxL1Active_;

    if (sh.sliceType_ == SliceType::P || sh.sliceType_ == SliceType::SP ||
        sh.sliceType_ == SliceType::B)
    {
        bool overrideFlag = br.readBit() != 0U;
        if (overrideFlag)
        {
            sh.numRefIdxActiveL0_ = static_cast<uint8_t>(br.readUev() + 1U);
            if (sh.sliceType_ == SliceType::B)
                sh.numRefIdxActiveL1_ = static_cast<uint8_t>(br.readUev() + 1U);
        }
    }

    // ref_pic_list_modification (skip for now — Phase 6)
    skipRefPicListModification(br, sh.sliceType_);

    // dec_ref_pic_marking (if nal_ref_idc != 0)
    if (nalRefIdc != 0U)
        skipDecRefPicMarking(br, isIdr, sh.decRefPicMarking_);

    // cabac_init_idc (only for CABAC slices)
    if (pps.isCabac() && sh.sliceType_ != SliceType::I && sh.sliceType_ != SliceType::SI)
        sh.cabacInitIdc_ = static_cast<uint8_t>(br.readUev());

    // slice_qp_delta
    sh.sliceQpDelta_ = br.readSev();

    // SP/SI specific (skip for now)
    if (sh.sliceType_ == SliceType::SP || sh.sliceType_ == SliceType::SI)
    {
        if (sh.sliceType_ == SliceType::SP)
            br.readBit(); // sp_for_switch_flag
        br.readSev();     // slice_qs_delta
    }

    // deblocking_filter_control
    if (pps.deblockingFilterControlPresent_)
    {
        sh.disableDeblockingFilter_ = static_cast<uint8_t>(br.readUev());
        if (sh.disableDeblockingFilter_ != 1U)
        {
            sh.sliceAlphaC0Offset_ = br.readSev() * 2;
            sh.sliceBetaOffset_ = br.readSev() * 2;
        }
    }

    return Result::Ok;
}

} // namespace sub0h264

#endif // CROG_SUB0H264_SLICE_HPP
