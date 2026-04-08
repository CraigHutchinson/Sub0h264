/** Spec-only CABAC Arithmetic Engine
 *
 *  Pure ITU-T H.264 implementation from spec equations and tables.
 *  No reference to any existing decoder implementation.
 *
 *  References:
 *    - ITU-T H.264 Section 9.3.1  (Initialization)
 *    - ITU-T H.264 Section 9.3.3  (Arithmetic decoding)
 *    - ITU-T H.264 Table 9-45     (rangeTabLPS)
 *    - ITU-T H.264 Table 9-46     (State transition after LPS)
 *    - ITU-T H.264 Table 9-47     (State transition after MPS)
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef CROG_SUB0H264_SPEC_REF_CABAC_HPP
#define CROG_SUB0H264_SPEC_REF_CABAC_HPP

#include "../../components/sub0h264/src/bitstream.hpp"

#include <cstdint>
#include <cassert>

namespace sub0h264 {
namespace spec_ref {

// ============================================================================
// ITU-T H.264 Table 9-45: rangeTabLPS[pStateIdx][qCodIRangeIdx]
// Columns indexed by qCodIRangeIdx = (codIRange >> 6) & 3
// ============================================================================
inline constexpr uint8_t cRangeTabLPS[64][4] = {
    {128,176,208,240}, {128,167,197,227}, {128,158,187,216}, {123,150,178,205},
    {116,142,169,195}, {111,135,160,185}, {105,128,152,175}, {100,122,144,166},
    { 95,116,137,158}, { 90,110,130,150}, { 85,104,123,142}, { 81, 99,117,135},
    { 77, 94,111,128}, { 73, 89,105,122}, { 69, 85,100,116}, { 66, 80, 95,110},
    { 62, 76, 90,104}, { 59, 72, 86, 99}, { 56, 69, 81, 94}, { 53, 65, 77, 89},
    { 51, 62, 73, 85}, { 48, 59, 69, 80}, { 46, 56, 66, 76}, { 43, 53, 63, 72},
    { 41, 50, 59, 69}, { 39, 48, 56, 65}, { 37, 45, 54, 62}, { 35, 43, 51, 59},
    { 33, 41, 48, 56}, { 32, 39, 46, 53}, { 30, 37, 43, 50}, { 29, 35, 41, 48},
    { 27, 33, 39, 45}, { 26, 31, 37, 43}, { 24, 30, 35, 41}, { 23, 28, 33, 39},
    { 22, 27, 32, 37}, { 21, 26, 30, 35}, { 20, 24, 29, 33}, { 19, 23, 27, 31},
    { 18, 22, 26, 30}, { 17, 21, 25, 28}, { 16, 20, 23, 27}, { 15, 19, 22, 25},
    { 14, 18, 21, 24}, { 14, 17, 20, 23}, { 13, 16, 19, 22}, { 12, 15, 18, 21},
    { 12, 14, 17, 20}, { 11, 14, 16, 19}, { 11, 13, 15, 18}, { 10, 12, 15, 17},
    { 10, 12, 14, 16}, {  9, 11, 13, 15}, {  9, 11, 12, 14}, {  8, 10, 12, 14},
    {  8,  9, 11, 13}, {  7,  9, 11, 12}, {  7,  9, 10, 12}, {  7,  8, 10, 11},
    {  6,  8,  9, 11}, {  6,  7,  9, 10}, {  6,  7,  8,  9}, {  2,  2,  2,  2},
};

// ITU-T H.264 Table 9-46: transIdxLPS — next pStateIdx after decoding LPS
inline constexpr uint8_t cTransIdxLPS[64] = {
     0,  0,  1,  2,  2,  4,  4,  5,
     6,  7,  8,  9,  9, 11, 11, 12,
    13, 13, 15, 15, 16, 16, 18, 18,
    19, 19, 21, 21, 22, 22, 23, 24,
    24, 25, 26, 26, 27, 27, 28, 29,
    29, 30, 30, 30, 31, 32, 32, 33,
    33, 33, 34, 34, 35, 35, 35, 36,
    36, 36, 37, 37, 37, 38, 38, 63,
};

// ITU-T H.264 Table 9-47: transIdxMPS — next pStateIdx after decoding MPS
inline constexpr uint8_t cTransIdxMPS[64] = {
     1,  2,  3,  4,  5,  6,  7,  8,
     9, 10, 11, 12, 13, 14, 15, 16,
    17, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 29, 30, 31, 32,
    33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48,
    49, 50, 51, 52, 53, 54, 55, 56,
    57, 58, 59, 60, 61, 62, 62, 63,
};

// ── Spot-checks against known spec values ──────────────────────────────
// ITU-T H.264 Table 9-45 row 0: highest probability LPS
static_assert(cRangeTabLPS[0][0] == 128, "Table 9-45 row 0 col 0");
static_assert(cRangeTabLPS[0][3] == 240, "Table 9-45 row 0 col 3");
// Row 63: equiprobable state used for terminate
static_assert(cRangeTabLPS[63][0] == 2, "Table 9-45 row 63 col 0");
static_assert(cRangeTabLPS[63][3] == 2, "Table 9-45 row 63 col 3");
// Mid-table spot check: row 12
static_assert(cRangeTabLPS[12][0] == 77, "Table 9-45 row 12 col 0");
static_assert(cRangeTabLPS[12][3] == 128, "Table 9-45 row 12 col 3");
// Row 31
static_assert(cRangeTabLPS[31][0] == 29, "Table 9-45 row 31 col 0");
static_assert(cRangeTabLPS[31][1] == 35, "Table 9-45 row 31 col 1");
// Transition table checks
static_assert(cTransIdxLPS[0] == 0, "Table 9-46 row 0");
static_assert(cTransIdxLPS[63] == 63, "Table 9-46 row 63");
static_assert(cTransIdxMPS[0] == 1, "Table 9-47 row 0");
static_assert(cTransIdxMPS[62] == 62, "Table 9-47 row 62: saturate at 62");
static_assert(cTransIdxMPS[63] == 63, "Table 9-47 row 63: stay at 63");

// ============================================================================
// CABAC Context Model — ITU-T H.264 Section 9.3.1.1
// ============================================================================

/** Single CABAC context: packed MPS flag (bit 6) + pStateIdx (bits 0-5). */
struct CabacCtx
{
    uint8_t state = 0U; ///< bits[5:0] = pStateIdx, bit[6] = valMPS

    uint8_t pStateIdx() const noexcept { return state & 0x3FU; }
    uint8_t valMPS() const noexcept { return (state >> 6U) & 1U; }

    void set(uint8_t pState, uint8_t mps) noexcept
    {
        state = static_cast<uint8_t>((mps << 6U) | (pState & 0x3FU));
    }
};

// ============================================================================
// CABAC Arithmetic Engine — ITU-T H.264 Section 9.3.3.2
// ============================================================================

/** CABAC binary arithmetic decoder, implemented per the spec pseudocode. */
class CabacEngine
{
public:
    CabacEngine() noexcept = default;

    /** Initialize the CABAC engine — ITU-T H.264 Section 9.3.1.2.
     *
     *  Reads 9 bits for codIOffset from the bitstream after slice header
     *  byte alignment. codIRange is initialized to 510.
     */
    void init(BitReader& br) noexcept
    {
        br_ = &br;
        // ITU-T H.264 Section 9.3.1.2:
        // codIRange = 510
        // codIOffset = read_bits(9)
        codIRange_ = 510U;
        codIOffset_ = br_->readBits(9U);
    }

    /** Decode a single bin using a context model — ITU-T H.264 Section 9.3.3.2.1.
     *
     *  @param ctx  The context model to use (updated in place)
     *  @return The decoded binary value (0 or 1)
     */
    uint32_t decodeBin(CabacCtx& ctx) noexcept
    {
        uint8_t pStateIdx = ctx.pStateIdx();
        uint8_t valMPS = ctx.valMPS();

        // ITU-T H.264 Section 9.3.3.2.1 step 1:
        // qCodIRangeIdx = (codIRange >> 6) & 3
        uint32_t qCodIRangeIdx = (codIRange_ >> 6U) & 3U;
        uint32_t codIRangeLPS = cRangeTabLPS[pStateIdx][qCodIRangeIdx];

        // ITU-T H.264 Section 9.3.3.2.1 step 2:
        // codIRange = codIRange - codIRangeLPS
        codIRange_ -= codIRangeLPS;

        uint32_t binVal;
        if (codIOffset_ >= codIRange_) {
            // LPS path
            binVal = 1U - valMPS;
            codIOffset_ -= codIRange_;
            codIRange_ = codIRangeLPS;

            if (pStateIdx == 0U) {
                // Swap MPS and LPS
                ctx.set(0U, 1U - valMPS);
            } else {
                ctx.set(cTransIdxLPS[pStateIdx], valMPS);
            }
        } else {
            // MPS path
            binVal = valMPS;
            ctx.set(cTransIdxMPS[pStateIdx], valMPS);
        }

        // Renormalization — ITU-T H.264 Section 9.3.3.2.2
        renormalize();

        return binVal;
    }

    /** Decode a bypass bin — ITU-T H.264 Section 9.3.3.2.3.
     *
     *  Used for bins that do not use context modeling (equiprobable).
     */
    uint32_t decodeBypass() noexcept
    {
        // ITU-T H.264 Section 9.3.3.2.3:
        // codIOffset = (codIOffset << 1) | read_bits(1)
        codIOffset_ = (codIOffset_ << 1U) | br_->readBit();

        if (codIOffset_ >= codIRange_) {
            codIOffset_ -= codIRange_;
            return 1U;
        }
        return 0U;
    }

    /** Decode the terminate bin — ITU-T H.264 Section 9.3.3.2.4.
     *
     *  Used for end_of_slice_flag and the terminate bin of mb_type in I slices.
     */
    uint32_t decodeTerminate() noexcept
    {
        // ITU-T H.264 Section 9.3.3.2.4:
        // codIRange -= 2
        codIRange_ -= 2U;

        if (codIOffset_ >= codIRange_) {
            return 1U; // end of slice
        }

        // Renormalize
        renormalize();
        return 0U;
    }

    /** Read remaining bits to align — used after CABAC slice decode. */
    uint32_t codIRange() const noexcept { return codIRange_; }
    uint32_t codIOffset() const noexcept { return codIOffset_; }

private:
    /** Renormalization — ITU-T H.264 Section 9.3.3.2.2.
     *
     *  While codIRange < 256, shift both codIRange and codIOffset left
     *  and read one bit into codIOffset.
     */
    void renormalize() noexcept
    {
        while (codIRange_ < 256U) {
            codIRange_ <<= 1U;
            codIOffset_ = (codIOffset_ << 1U) | br_->readBit();
        }
    }

    BitReader* br_ = nullptr;
    uint32_t codIRange_ = 0U;
    uint32_t codIOffset_ = 0U;
};

} // namespace spec_ref
} // namespace sub0h264

#endif // CROG_SUB0H264_SPEC_REF_CABAC_HPP
