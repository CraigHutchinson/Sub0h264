/** Spec-only CABAC Context Initialization Tables
 *
 *  ITU-T H.264 Section 9.3.1.1, Tables 9-12 through 9-24.
 *  All 460 (m,n) pairs for I-slice context initialization.
 *
 *  The initialization formula (Section 9.3.1.1):
 *    preCtxState = Clip3(1, 126, ((m * SliceQPY) >> 4) + n)
 *    if (preCtxState <= 63):
 *        pStateIdx = 63 - preCtxState
 *        valMPS = 0
 *    else:
 *        pStateIdx = preCtxState - 64
 *        valMPS = 1
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef CROG_SUB0H264_SPEC_REF_CABAC_INIT_HPP
#define CROG_SUB0H264_SPEC_REF_CABAC_INIT_HPP

#include "spec_ref_cabac.hpp"

#include <cstdint>
#include <algorithm>

namespace sub0h264 {
namespace spec_ref {

/// Total number of CABAC context models — ITU-T H.264 Table 9-11.
inline constexpr uint32_t cNumCabacContexts = 460U;

/// Context index for end_of_slice_flag — ITU-T H.264 Table 9-35.
inline constexpr uint32_t cCtxIdxEndOfSlice = 276U;

// ============================================================================
// ITU-T H.264 Tables 9-12 through 9-24: (m,n) initialization pairs
// for I-slice (cabac_init_idc is not applicable for I/SI slices;
// the spec uses a single table set for I-slices).
// ============================================================================

/// (m,n) pairs for CABAC context initialization in I-slices.
/// Index = ctxIdx (0..459), values[0] = m, values[1] = n.
inline constexpr int8_t cCabacInitI[460][2] = {
    // -- Table 9-12: ctxIdx 0..10 (mb_type for SI/I slices) ----------------
    { 20, -15}, {  2,  54}, {  3,  74}, { 20, -15}, {  2,  54},
    {  3,  74}, {-28, 127}, {-23, 104}, { -6,  53}, { -1,  54},
    {  7,  51},

    // -- Table 9-13: ctxIdx 11..23 (mb_type for P/SP slices -- unused for I)
    {  0,   0}, {  0,   0}, {  0,   0}, {  0,   0}, {  0,   0},
    {  0,   0}, {  0,   0}, {  0,   0}, {  0,   0}, {  0,   0},
    {  0,   0}, {  0,   0}, {  0,   0},

    // -- Table 9-14: ctxIdx 24..39 (mb_type for B slices -- unused for I) --
    {  0,   0}, {  0,   0}, {  0,   0}, {  0,   0}, {  0,   0},
    {  0,   0}, {  0,   0}, {  0,   0}, {  0,   0}, {  0,   0},
    {  0,   0}, {  0,   0}, {  0,   0}, {  0,   0}, {  0,   0},
    {  0,   0},

    // -- Table 9-15: ctxIdx 40..53 (sub_mb_type -- unused for I) -----------
    {  0,   0}, {  0,   0}, {  0,   0}, {  0,   0}, {  0,   0},
    {  0,   0}, {  0,   0}, {  0,   0}, {  0,   0}, {  0,   0},
    {  0,   0}, {  0,   0}, {  0,   0}, {  0,   0},

    // -- Table 9-16: ctxIdx 54..59 (ref_idx -- unused for I) ---------------
    {  0,   0}, {  0,   0}, {  0,   0}, {  0,   0}, {  0,   0},
    {  0,   0},

    // -- Table 9-17: ctxIdx 60..69 (mb_qp_delta) --------------------------
    {  0,  41}, {  0,  63}, {  0,  63}, {  0,  63}, { -9,  83},
    {  4,  86}, {  0,  97}, { -7,  72}, { 13,  41}, {  3,  62},

    // -- Table 9-18: ctxIdx 70..104 (intra pred + coded_block_pattern)
    // ctxIdx 70..87
    {  0,  11}, {  1,  55}, {  0,  69}, {-17, 127}, {-13, 102},
    {  0,  82}, { -7,  74}, {-21, 107}, {-27, 127}, {-31, 127},
    {-24, 127}, {-18,  95}, {-27, 127}, {-21, 114}, {-30, 127},
    {-17, 123}, {-12, 115}, {-16, 122},
    // ctxIdx 88..104
    {-11, 115}, {-12,  63}, { -2,  68}, {-15,  84}, {-13, 104},
    { -3,  70}, { -8,  93}, {-10,  90}, {-30, 127}, { -1,  74},
    { -6,  97}, { -7,  91}, {-20, 127}, { -4,  56}, { -5,  82},
    { -7,  76}, {-22, 125},

    // -- Table 9-19: ctxIdx 105..165 (significant_coeff_flag + last_significant for 4x4)
    // ctxIdx 105..135
    { -7,  93}, {-11,  87}, { -3,  77}, { -5,  71}, { -4,  63},
    { -4,  68}, {-12,  84}, { -7,  62}, { -7,  65}, {  8,  61},
    {  5,  56}, { -2,  66}, {  1,  64}, {  0,  61}, { -2,  78},
    {  1,  50}, {  7,  52}, { 10,  35}, {  0,  44}, { 11,  38},
    {  1,  45}, {  0,  46}, {  5,  44}, { 31,  17}, {  1,  51},
    {  7,  50}, { 28,  19}, { 16,  33}, { 14,  62}, {-13, 108},
    {-15, 100},
    // ctxIdx 136..165
    {-13, 101}, {-13,  91}, {-12,  94}, {-10,  88}, {-16,  84},
    {-10,  86}, { -7,  83}, {-13,  87}, {-19,  94}, {  1,  70},
    {  0,  72}, { -5,  74}, { 18,  59}, { -8, 102}, {-15, 100},
    {  0,  95}, { -4,  75}, {  2,  72}, {-11,  75}, { -3,  71},
    { 15,  46}, {-13,  69}, {  0,  62}, {  0,  65}, { 21,  37},
    {-15,  72}, {  9,  57}, { 16,  54}, {  0,  62}, { 12,  72},

    // -- Table 9-20: ctxIdx 166..226 (coeff_abs_level_minus1 prefix/suffix)
    // ctxIdx 166..196
    { 24,   0}, { 15,   9}, {  8,  25}, { 13,  18}, { 15,   9},
    { 13,  19}, { 10,  37}, { 12,  18}, {  6,  29}, { 20,  33},
    { 15,  30}, {  4,  45}, {  1,  58}, {  0,  62}, {  7,  61},
    { 12,  38}, { 11,  45}, { 15,  39}, { 11,  42}, { 13,  44},
    { 16,  45}, { 12,  41}, { 10,  49}, { 30,  34}, { 18,  42},
    { 10,  55}, { 17,  51}, { 17,  46}, {  0,  89}, { 26, -19},
    { 22, -17},
    // ctxIdx 197..226
    { 26, -17}, { 30, -25}, { 28, -20}, { 33, -23}, { 37, -27},
    { 33, -23}, { 40, -28}, { 38, -17}, { 33, -11}, { 40, -15},
    { 41,  -6}, { 38,   1}, { 41,  17}, { 30,  -6}, { 27,   3},
    { 26,  22}, { 37, -16}, { 35,  -4}, { 38,  -8}, { 38,  -3},
    { 37,   3}, { 38,   5}, { 42,   0}, { 35,  16}, { 39,  22},
    { 14,  48}, { 27,  37}, { 21,  60}, { 12,  68}, {  2,  97},

    // -- Table 9-21: ctxIdx 227..275 (coeff_abs_level contexts continued)
    // ctxIdx 227..251
    { -3,  71}, { -6,  42}, { -5,  50}, { -3,  54}, { -2,  62},
    {  0,  58}, {  1,  63}, { -2,  72}, { -1,  74}, { -9,  91},
    { -5,  67}, { -5,  27}, { -3,  39}, { -2,  44}, {  0,  46},
    {-16,  64}, { -8,  68}, {-10,  78}, { -6,  77}, {-10,  86},
    {-12,  92}, {-15,  55}, {-10,  60}, { -6,  62}, { -4,  65},
    // ctxIdx 252..275
    {-12,  73}, { -8,  76}, { -7,  80}, { -9,  88}, {-17, 110},
    {-11,  97}, {-20,  84}, {-11,  79}, { -6,  73}, { -4,  74},
    {-13,  86}, {-13,  96}, {-11,  97}, {-19, 117}, { -8,  78},
    { -5,  33}, { -4,  48}, { -2,  53}, { -3,  62}, {-13,  71},
    {-10,  79}, {-12,  86}, {-13,  90}, {-14,  97},

    // -- ctxIdx 276: end_of_slice_flag (special)
    {  0,   0},

    // -- Table 9-22: ctxIdx 277..337 (significant_coeff_flag + last_significant for 8x8)
    // ctxIdx 277..307
    { -6,  93}, { -6,  84}, { -8,  79}, {  0,  66}, { -1,  71},
    {  0,  62}, { -2,  60}, { -2,  59}, { -5,  75}, { -3,  62},
    { -4,  58}, { -9,  66}, { -1,  79}, {  0,  71}, {  3,  68},
    { 10,  44}, { -7,  62}, { 15,  36}, { 14,  40}, { 16,  27},
    { 12,  29}, {  1,  44}, { 20,  36}, { 18,  32}, {  5,  42},
    {  1,  48}, { 10,  62}, { 17,  46}, {  9,  64}, {-12, 104},
    {-11,  97},
    // ctxIdx 308..337
    {-16,  96}, { -7,  88}, { -8,  85}, { -7,  85}, { -9,  85},
    {-13,  88}, {  4,  66}, { -3,  77}, { -3,  76}, { -6,  76},
    { 10,  58}, { -1,  76}, { -1,  83}, { -7,  99}, {-14,  95},
    {  2,  95}, {  0,  76}, { -5,  74}, {  0,  70}, {-11,  75},
    {  1,  68}, {  0,  65}, {-14,  73}, {  3,  62}, {  4,  62},
    { -1,  68}, {-13,  75}, { 11,  55}, {  5,  64}, { 12,  70},

    // -- Table 9-23: ctxIdx 338..398 (coeff_abs_level for 8x8)
    // ctxIdx 338..368
    { 15,   6}, {  6,  19}, {  7,  16}, { 12,  14}, { 18,  13},
    { 13,  11}, { 13,  15}, { 15,  16}, { 12,  23}, { 13,  23},
    { 15,  20}, { 14,  26}, { 14,  44}, { 17,  40}, { 17,  47},
    { 24,  17}, { 21,  21}, { 25,  22}, { 31,  27}, { 22,  29},
    { 19,  35}, { 14,  50}, { 10,  57}, {  7,  63}, { -2,  77},
    { -4,  82}, { -3,  94}, {  9,  69}, {-12, 109}, { 36, -35},
    { 36, -34},
    // ctxIdx 369..398
    { 32, -26}, { 37, -30}, { 44, -32}, { 34, -18}, { 34, -15},
    { 40, -15}, { 33,  -7}, { 35,  -5}, { 33,   0}, { 38,   2},
    { 33,  13}, { 23,  35}, { 13,  58}, { 29,  -3}, { 26,   0},
    { 22,  30}, { 31,  -7}, { 35, -15}, { 34,  -3}, { 34,   3},
    { 36,  -1}, { 34,   5}, { 32,  11}, { 35,   5}, { 34,  12},
    { 39,  11}, { 30,  29}, { 34,  26}, { 29,  39}, { 19,  66},

    // -- Table 9-16 (continued): ctxIdx 399..401 (ref_idx for B slices)
    { 31,  21}, { 31,  31}, { 25,  50},

    // -- Table 9-24: ctxIdx 402..459 (significant/last/level for chroma DC + 8x8)
    // ctxIdx 402..435
    {-17, 120}, {-20, 112}, {-18, 114}, {-11,  85}, {-15,  92},
    {-14,  89}, {-26,  71}, {-15,  81}, {-14,  80}, {  0,  68},
    {-14,  70}, {-24,  56}, {-23,  68}, {-24,  50}, {-11,  74},
    { 23, -13}, { 26, -13}, { 40, -15}, { 49, -14}, { 44,   3},
    { 45,   6}, { 44,  34}, { 33,  54}, { 19,  82}, { -3,  75},
    { -1,  23}, {  1,  34}, {  1,  43}, {  0,  54}, { -2,  55},
    {  0,  61}, {  1,  64}, {  0,  68}, { -9,  92},
    // ctxIdx 436..459
    {-14, 106}, {-13,  97}, {-15,  90}, {-12,  90}, {-18,  88},
    {-10,  73}, { -9,  79}, {-14,  86}, {-10,  73}, {-10,  70},
    {-10,  69}, { -5,  66}, { -9,  64}, { -5,  58}, {  2,  59},
    { 21, -10}, { 24, -11}, { 28,  -8}, { 28,  -1}, { 29,   3},
    { 29,   9}, { 35,  20}, { 29,  36}, { 14,  67},
};

// -- Spot-checks for init table -----------------------------------------
static_assert(cCabacInitI[0][0] == 20 && cCabacInitI[0][1] == -15,
              "Table 9-12 ctxIdx 0");
static_assert(cCabacInitI[60][0] == 0 && cCabacInitI[60][1] == 41,
              "Table 9-17 ctxIdx 60");
static_assert(cCabacInitI[70][0] == 0 && cCabacInitI[70][1] == 11,
              "Table 9-18 ctxIdx 70");
static_assert(cCabacInitI[276][0] == 0 && cCabacInitI[276][1] == 0,
              "ctxIdx 276 end_of_slice");
static_assert(cCabacInitI[277][0] == -6 && cCabacInitI[277][1] == 93,
              "Table 9-22 ctxIdx 277");
static_assert(cCabacInitI[459][0] == 14 && cCabacInitI[459][1] == 67,
              "Table 9-24 ctxIdx 459");

// ============================================================================
// Context Initialization Function -- ITU-T H.264 Section 9.3.1.1
// ============================================================================

/** Clip a value to the range [low, high]. */
inline constexpr int32_t clip3(int32_t low, int32_t high, int32_t val) noexcept
{
    return val < low ? low : (val > high ? high : val);
}

/** Initialize all CABAC context models for an I-slice.
 *
 *  ITU-T H.264 Section 9.3.1.1:
 *  For each context model with index ctxIdx:
 *    preCtxState = Clip3(1, 126, ((m * SliceQPY) >> 4) + n)
 *    if (preCtxState <= 63): pStateIdx = 63 - preCtxState, valMPS = 0
 *    else: pStateIdx = preCtxState - 64, valMPS = 1
 *
 *  @param contexts  Array of cNumCabacContexts contexts to initialize
 *  @param sliceQpY  The slice QP value (0..51)
 */
inline void initCabacContexts(CabacCtx* contexts, int32_t sliceQpY) noexcept
{
    for (uint32_t ctxIdx = 0U; ctxIdx < cNumCabacContexts; ++ctxIdx) {
        int32_t m = cCabacInitI[ctxIdx][0];
        int32_t n = cCabacInitI[ctxIdx][1];

        // ITU-T H.264 Section 9.3.1.1 equation
        int32_t preCtxState = clip3(1, 126, ((m * sliceQpY) >> 4) + n);

        uint8_t pStateIdx;
        uint8_t valMPS;
        if (preCtxState <= 63) {
            pStateIdx = static_cast<uint8_t>(63 - preCtxState);
            valMPS = 0U;
        } else {
            pStateIdx = static_cast<uint8_t>(preCtxState - 64);
            valMPS = 1U;
        }

        contexts[ctxIdx].set(pStateIdx, valMPS);
    }
}

} // namespace spec_ref
} // namespace sub0h264

#endif // CROG_SUB0H264_SPEC_REF_CABAC_INIT_HPP
