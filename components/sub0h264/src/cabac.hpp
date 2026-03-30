/** Sub0h264 — CABAC entropy decoder
 *
 *  Context-Adaptive Binary Arithmetic Coding for H.264 High profile.
 *  Implements the arithmetic decode engine, context model management,
 *  and binarization schemes for all syntax elements.
 *
 *  Reference: ITU-T H.264 §9.3
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef CROG_SUB0H264_CABAC_HPP
#define CROG_SUB0H264_CABAC_HPP

#include "bitstream.hpp"

#include <cstdint>
#include <cstring>
#include <array>

namespace sub0h264 {

/// Number of CABAC context models — ITU-T H.264 Table 9-11 through 9-23.
inline constexpr uint32_t cNumCabacCtx = 460U;

/// Maximum QP value.
inline constexpr int32_t cMaxQp = 51;

// ── CABAC context model ─────────────────────────────────────────────────

/** Single CABAC binary context model.
 *  Bit 6 = MPS (Most Probable Symbol), Bits 0-5 = state index [0-63].
 */
struct CabacCtx
{
    uint8_t mpsState = 0U;

    uint8_t mps() const noexcept { return (mpsState >> 6U) & 1U; }
    uint8_t state() const noexcept { return mpsState & 0x3FU; }
};

// ── Combined CABAC table — rangeTabLPS + state transitions ──────────────

/** Combined table: rangeTabLPS, nextStateMPS, nextStateLPS.
 *
 *  Indexed by [combined_state][quantized_range].
 *  combined_state = (state << 1) | mps, so 128 entries (64 states × 2 MPS).
 *
 *  Each 32-bit entry packs:
 *    bits  0-7:  rangeTabLPS value
 *    bits  8-14: next combined_state if MPS
 *    bits 15-21: next combined_state if LPS
 *
 *  From libavc gau4_ih264_cabac_table[128][4].
 */
inline constexpr uint32_t cCabacTable[128][4] = {
    { 2097536, 2097584, 2097616, 2097648 },
    { 640, 679, 709, 739 },
    { 33664, 33694, 33723, 33752 },
    { 66683, 66710, 66738, 66765 },
    { 66932, 66958, 66985, 67011 },
    { 132719, 132743, 132768, 132793 },
    { 132969, 132992, 133016, 133039 },
    { 165988, 166010, 166032, 166054 },
    { 199007, 199028, 199049, 199070 },
    { 232026, 232046, 232066, 232086 },
    { 265045, 265064, 265083, 265102 },
    { 298065, 298083, 298101, 298119 },
    { 298317, 298334, 298351, 298368 },
    { 364105, 364121, 364137, 364154 },
    { 364357, 364373, 364388, 364404 },
    { 397378, 397392, 397407, 397422 },
    { 430398, 430412, 430426, 430440 },
    { 430651, 430664, 430678, 430691 },
    { 496440, 496453, 496465, 496478 },
    { 496693, 496705, 496717, 496729 },
    { 529715, 529726, 529737, 529749 },
    { 529968, 529979, 529989, 530000 },
    { 595758, 595768, 595778, 595788 },
    { 596011, 596021, 596031, 596040 },
    { 629033, 629042, 629051, 629061 },
    { 629287, 629296, 629304, 629313 },
    { 695077, 695085, 695094, 695102 },
    { 695331, 695339, 695347, 695355 },
    { 728353, 728361, 728368, 728376 },
    { 728608, 728615, 728622, 728629 },
    { 761630, 761637, 761643, 761650 },
    { 794653, 794659, 794665, 794672 },
    { 794907, 794913, 794919, 794925 },
    { 827930, 827935, 827941, 827947 },
    { 860952, 860958, 860963, 860969 },
    { 861207, 861212, 861217, 861223 },
    { 894230, 894235, 894240, 894245 },
    { 894485, 894490, 894494, 894499 },
    { 927508, 927512, 927517, 927521 },
    { 960531, 960535, 960539, 960543 },
    { 960786, 960790, 960794, 960798 },
    { 993809, 993813, 993817, 993820 },
    { 994064, 994068, 994071, 994075 },
    { 994319, 994323, 994326, 994329 },
    { 1027342, 1027346, 1027349, 1027352 },
    { 1060366, 1060369, 1060372, 1060375 },
    { 1060621, 1060624, 1060627, 1060630 },
    { 1093644, 1093647, 1093650, 1093653 },
    { 1093900, 1093902, 1093905, 1093908 },
    { 1094155, 1094158, 1094160, 1094163 },
    { 1127179, 1127181, 1127183, 1127186 },
    { 1127434, 1127436, 1127439, 1127441 },
    { 1160458, 1160460, 1160462, 1160464 },
    { 1160713, 1160715, 1160717, 1160719 },
    { 1160969, 1160971, 1160972, 1160974 },
    { 1193992, 1193994, 1193996, 1193998 },
    { 1194248, 1194249, 1194251, 1194253 },
    { 1194503, 1194505, 1194507, 1194508 },
    { 1227527, 1227529, 1227530, 1227532 },
    { 1227783, 1227784, 1227786, 1227787 },
    { 1228038, 1228040, 1228041, 1228043 },
    { 1261062, 1261063, 1261065, 1261066 },
    { 1261062, 1261063, 1261064, 1261065 },
    { 2080514, 2080514, 2080514, 2080514 },
    // States 64-127 (MPS=1)
    { 16768, 16816, 16848, 16880 },
    { 2114176, 2114215, 2114245, 2114275 },
    { 2147200, 2147230, 2147259, 2147288 },
    { 2180219, 2180246, 2180274, 2180301 },
    { 2180468, 2180494, 2180521, 2180547 },
    { 2246255, 2246279, 2246304, 2246329 },
    { 2246505, 2246528, 2246552, 2246575 },
    { 2279524, 2279546, 2279568, 2279590 },
    { 2312543, 2312564, 2312585, 2312606 },
    { 2345562, 2345582, 2345602, 2345622 },
    { 2378581, 2378600, 2378619, 2378638 },
    { 2411601, 2411619, 2411637, 2411655 },
    { 2411853, 2411870, 2411887, 2411904 },
    { 2477641, 2477657, 2477673, 2477690 },
    { 2477893, 2477909, 2477924, 2477940 },
    { 2510914, 2510928, 2510943, 2510958 },
    { 2543934, 2543948, 2543962, 2543976 },
    { 2544187, 2544200, 2544214, 2544227 },
    { 2609976, 2609989, 2610001, 2610014 },
    { 2610229, 2610241, 2610253, 2610265 },
    { 2643251, 2643262, 2643273, 2643285 },
    { 2643504, 2643515, 2643525, 2643536 },
    { 2709294, 2709304, 2709314, 2709324 },
    { 2709547, 2709557, 2709567, 2709576 },
    { 2742569, 2742578, 2742587, 2742597 },
    { 2742823, 2742832, 2742840, 2742849 },
    { 2808613, 2808621, 2808630, 2808638 },
    { 2808867, 2808875, 2808883, 2808891 },
    { 2841889, 2841897, 2841904, 2841912 },
    { 2842144, 2842151, 2842158, 2842165 },
    { 2875166, 2875173, 2875179, 2875186 },
    { 2908189, 2908195, 2908201, 2908208 },
    { 2908443, 2908449, 2908455, 2908461 },
    { 2941466, 2941471, 2941477, 2941483 },
    { 2974488, 2974494, 2974499, 2974505 },
    { 2974743, 2974748, 2974753, 2974759 },
    { 3007766, 3007771, 3007776, 3007781 },
    { 3008021, 3008026, 3008030, 3008035 },
    { 3041044, 3041048, 3041053, 3041057 },
    { 3074067, 3074071, 3074075, 3074079 },
    { 3074322, 3074326, 3074330, 3074334 },
    { 3107345, 3107349, 3107353, 3107356 },
    { 3107600, 3107604, 3107607, 3107611 },
    { 3107855, 3107859, 3107862, 3107865 },
    { 3140878, 3140882, 3140885, 3140888 },
    { 3173902, 3173905, 3173908, 3173911 },
    { 3174157, 3174160, 3174163, 3174166 },
    { 3207180, 3207183, 3207186, 3207189 },
    { 3207436, 3207438, 3207441, 3207444 },
    { 3207691, 3207694, 3207696, 3207699 },
    { 3240715, 3240717, 3240719, 3240722 },
    { 3240970, 3240972, 3240975, 3240977 },
    { 3273994, 3273996, 3273998, 3274000 },
    { 3274249, 3274251, 3274253, 3274255 },
    { 3274505, 3274507, 3274508, 3274510 },
    { 3307528, 3307530, 3307532, 3307534 },
    { 3307784, 3307785, 3307787, 3307789 },
    { 3308039, 3308041, 3308043, 3308044 },
    { 3341063, 3341065, 3341066, 3341068 },
    { 3341319, 3341320, 3341322, 3341323 },
    { 3341574, 3341576, 3341577, 3341579 },
    { 3374598, 3374599, 3374601, 3374602 },
    { 3374598, 3374599, 3374600, 3374601 },
    { 4194050, 4194050, 4194050, 4194050 },
};

// ── Count leading zeros ─────────────────────────────────────────────────

inline uint32_t clz32(uint32_t val) noexcept
{
    if (val == 0U) return 32U;
#if defined(__GNUC__) || defined(__clang__)
    return static_cast<uint32_t>(__builtin_clz(val));
#elif defined(_MSC_VER)
    unsigned long idx;
    _BitScanReverse(&idx, val);
    return 31U - idx;
#else
    uint32_t n = 0U;
    if (val <= 0x0000FFFFU) { n += 16U; val <<= 16U; }
    if (val <= 0x00FFFFFFU) { n += 8U;  val <<= 8U; }
    if (val <= 0x0FFFFFFFU) { n += 4U;  val <<= 4U; }
    if (val <= 0x3FFFFFFFU) { n += 2U;  val <<= 2U; }
    if (val <= 0x7FFFFFFFU) { n += 1U; }
    return n;
#endif
}

// ── CABAC arithmetic decoder engine ─────────────────────────────────────

/** CABAC arithmetic decoding engine.
 *
 *  Maintains codIRange and codIOffset state for binary arithmetic decoding.
 *  Uses the combined rangeTabLPS/state transition table.
 */
class CabacEngine
{
public:
    CabacEngine() = default;

    /** Initialize engine from bitstream at current position.
     *  Reads 9 bits for initial offset per spec §9.3.1.2.
     */
    void init(BitReader& br) noexcept
    {
        br_ = &br;
        codIRange_ = 510U;
        codIOffset_ = br.readBits(9U);
    }

    /** Decode one binary decision using a context model.
     *  @param ctx  Context model (updated in-place)
     *  @return Decoded binary symbol (0 or 1)
     */
    uint32_t decodeBin(CabacCtx& ctx) noexcept
    {
        uint32_t state = ctx.mpsState & 0x7FU; // combined state = (pStateIdx << 1) | mps
        uint32_t qRange = (codIRange_ >> 6U) & 3U;

        uint32_t tableEntry = cCabacTable[state][qRange];
        uint32_t rangeLPS = tableEntry & 0xFFU;
        codIRange_ -= rangeLPS;

        uint32_t symbol;
        if (codIOffset_ >= codIRange_)
        {
            // LPS
            symbol = 1U - (state >> 6U); // 1 - MPS
            codIOffset_ -= codIRange_;
            codIRange_ = rangeLPS;
            ctx.mpsState = static_cast<uint8_t>((tableEntry >> 15U) & 0x7FU);
        }
        else
        {
            // MPS
            symbol = state >> 6U;
            ctx.mpsState = static_cast<uint8_t>((tableEntry >> 8U) & 0x7FU);
        }

        // Renormalize
        if (codIRange_ < 256U)
            renormalize();

        return symbol;
    }

    /** Decode one bypass (equiprobable) bin. */
    uint32_t decodeBypass() noexcept
    {
        codIOffset_ = (codIOffset_ << 1U) | br_->readBit();
        uint32_t symbol;
        if (codIOffset_ >= codIRange_)
        {
            symbol = 1U;
            codIOffset_ -= codIRange_;
        }
        else
        {
            symbol = 0U;
        }
        return symbol;
    }

    /** Decode N bypass bins, returning the N-bit value. */
    uint32_t decodeBypassBins(uint32_t numBins) noexcept
    {
        uint32_t val = 0U;
        for (uint32_t i = 0U; i < numBins; ++i)
            val = (val << 1U) | decodeBypass();
        return val;
    }

    /** Decode terminate bin (end-of-slice test). */
    uint32_t decodeTerminate() noexcept
    {
        codIRange_ -= 2U;
        if (codIOffset_ >= codIRange_)
            return 1U;
        if (codIRange_ < 256U)
            renormalize();
        return 0U;
    }

    /** Decode a unary-coded value with context switching.
     *  @param maxBins  Maximum number of bins
     *  @param ctxBase  Array of context models
     *  @param ctxIncFirst  Context increment for first bin
     *  @param ctxIncRest   Context increment for subsequent bins
     *  @return Decoded unsigned value
     */
    uint32_t decodeUnary(uint32_t maxBins, CabacCtx* ctxBase,
                          uint32_t ctxIncFirst, uint32_t ctxIncRest) noexcept
    {
        uint32_t val = 0U;
        if (decodeBin(ctxBase[ctxIncFirst]) == 0U)
            return 0U;
        ++val;
        while (val < maxBins)
        {
            if (decodeBin(ctxBase[ctxIncRest]) == 0U)
                break;
            ++val;
        }
        return val;
    }

    /** Decode a truncated unary value (UEG0 prefix). */
    uint32_t decodeTruncUnary(uint32_t maxVal, CabacCtx* ctxBase,
                               const uint32_t* ctxIncs, uint32_t numCtxIncs) noexcept
    {
        uint32_t val = 0U;
        while (val < maxVal)
        {
            uint32_t ctxInc = (val < numCtxIncs) ? ctxIncs[val] : ctxIncs[numCtxIncs - 1U];
            if (decodeBin(ctxBase[ctxInc]) == 0U)
                break;
            ++val;
        }
        return val;
    }

private:
    BitReader* br_ = nullptr;
    uint32_t codIRange_ = 510U;
    uint32_t codIOffset_ = 0U;

    void renormalize() noexcept
    {
        while (codIRange_ < 256U)
        {
            codIRange_ <<= 1U;
            codIOffset_ = (codIOffset_ << 1U) | br_->readBit();
        }
    }
};

// ── Context initialization — ITU-T H.264 §9.3.1.1 ──────────────────────

#include "cabac_init_mn.hpp"

/** Compute a single CABAC context initial state from (m, n) parameters.
 *
 *  Formula (ITU-T H.264 §9.3.1.1):
 *    preCtxState = Clip3(1, 126, ((m * SliceQPY) >> 4) + n)
 *    if preCtxState <= 63: pStateIdx = 63 - preCtxState, valMPS = 0
 *    else:                 pStateIdx = preCtxState - 64,  valMPS = 1
 *
 *  Returns packed mpsState: (pStateIdx & 0x3F) | (valMPS << 6)
 *  which matches our CabacCtx encoding and cCabacTable[128][4] index.
 */
inline constexpr uint8_t computeCabacInitState(int32_t m, int32_t n,
                                                int32_t sliceQpY) noexcept
{
    int32_t preCtxState = ((m * sliceQpY) >> 4) + n;
    // Clip3(1, 126, preCtxState)
    if (preCtxState < 1) preCtxState = 1;
    if (preCtxState > 126) preCtxState = 126;

    uint8_t pStateIdx;
    uint8_t valMPS;
    if (preCtxState <= 63)
    {
        pStateIdx = static_cast<uint8_t>(63 - preCtxState);
        valMPS = 0U;
    }
    else
    {
        pStateIdx = static_cast<uint8_t>(preCtxState - 64);
        valMPS = 1U;
    }

    return (pStateIdx & 0x3FU) | (valMPS << 6U);
}

/** Initialize CABAC context models for a slice.
 *
 *  Uses constexpr (m, n) parameters from ITU-T H.264 Tables 9-12 through 9-23
 *  to compute initial probability states. The computation is:
 *    preCtxState = Clip3(1, 126, ((m * SliceQPY) >> 4) + n)
 *
 *  @param[out] ctx       Array of 460 context models
 *  @param sliceType      0=P, 1=B, 2=I
 *  @param cabacInitIdc   PPS cabac_init_idc [0-2] (ignored for I-slices)
 *  @param sliceQpY       Slice QP Y value [0-51]
 */
inline void initCabacContexts(CabacCtx* ctx, uint32_t sliceType,
                               uint32_t cabacInitIdc, int32_t sliceQpY) noexcept
{
    // I-slices always use init_idc 3 (spec §9.3.1.1)
    /// Index into cCabacInitMN: 0-2 for P/B slices, 3 for I-slices.
    static constexpr uint32_t cISliceInitIdc = 3U;
    uint32_t idc = (sliceType == 2U) ? cISliceInitIdc : cabacInitIdc;
    if (idc > 3U) idc = 0U;

    for (uint32_t i = 0U; i < cNumCabacCtx; ++i)
    {
        int32_t m = cCabacInitMN[idc][i][0];
        int32_t n = cCabacInitMN[idc][i][1];
        ctx[i].mpsState = computeCabacInitState(m, n, sliceQpY);
    }
}

} // namespace sub0h264

#endif // CROG_SUB0H264_CABAC_HPP
