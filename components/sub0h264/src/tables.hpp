/** Sub0h264 — H.264 lookup tables
 *
 *  constexpr tables for CAVLC decoding, zigzag scan, CBP mapping.
 *  All tables computed/stored at compile time for flash efficiency.
 *
 *  Reference: ITU-T H.264 §9.2 (CAVLC), Tables 9-5 through 9-10
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef CROG_SUB0H264_TABLES_HPP
#define CROG_SUB0H264_TABLES_HPP

#include <cstdint>
#include <array>

namespace sub0h264 {

// ── Zigzag scan order — ITU-T H.264 §8.5.6 ─────────────────────────────

/// Inverse zigzag scan for 4x4 block (maps scan position → raster position).
inline constexpr std::array<uint8_t, 16> cZigzag4x4 = {
    0,  1,  4,  8,
    5,  2,  3,  6,
    9, 12, 13, 10,
    7, 11, 14, 15,
};

/// Inverse zigzag scan for 8x8 block — ITU-T H.264 §8.5.6.
inline constexpr std::array<uint8_t, 64> cZigzag8x8 = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63,
};

// ── Luma 4x4 block scan order — ITU-T H.264 §6.4.3 / Table 6-2 ────────

/// Pixel X offset within MB for each luma4x4BlkIdx in spec scan order.
/// The scan groups four 4x4 blocks within each 8x8 partition:
///   i8x8=0: blk 0(0,0) 1(4,0) 2(0,4) 3(4,4)
///   i8x8=1: blk 4(8,0) 5(12,0) 6(8,4) 7(12,4)
///   i8x8=2: blk 8(0,8) 9(4,8) 10(0,12) 11(4,12)
///   i8x8=3: blk 12(8,8) 13(12,8) 14(8,12) 15(12,12)
inline constexpr std::array<uint8_t, 16> cLuma4x4BlkX = {
     0,  4,  0,  4,   8, 12,  8, 12,   0,  4,  0,  4,   8, 12,  8, 12
};

/// Pixel Y offset within MB for each luma4x4BlkIdx in spec scan order.
inline constexpr std::array<uint8_t, 16> cLuma4x4BlkY = {
     0,  0,  4,  4,   0,  0,  4,  4,   8,  8, 12, 12,   8,  8, 12, 12
};

/// Maps luma4x4BlkIdx (spec scan order) → raster index (row-major 4x4 grid).
/// Raster layout:  0  1  2  3 /  4  5  6  7 /  8  9 10 11 / 12 13 14 15
/// Used for NNZ and mode arrays that are stored in raster order.
inline constexpr std::array<uint8_t, 16> cLuma4x4ToRaster = {
     0,  1,  4,  5,   2,  3,  6,  7,   8,  9, 12, 13,  10, 11, 14, 15
};

/// Maps raster index → luma4x4BlkIdx (spec scan order).
/// This is the inverse of cLuma4x4ToRaster (and happens to be the same
/// permutation — it is an involution).
inline constexpr std::array<uint8_t, 16> cRasterToLuma4x4 = {
     0,  1,  4,  5,   2,  3,  6,  7,   8,  9, 12, 13,  10, 11, 14, 15
};

/// Top-right 4x4 block availability indexed by luma4x4BlkIdx (spec scan order).
/// True when top-right hasn't been decoded yet (higher scan index) or lies
/// beyond the MB right edge (blkX=12). Does NOT cover frame boundary —
/// caller must also check absX + 4 < frameWidth.
/// Reference: ITU-T H.264 §6.4.11.
inline constexpr std::array<bool, 16> cTopRightUnavailScan = {
    false, false, false, true,   // scan 0-3: 3(4,4)→TR=scan4
    false, false, false, true,   // scan 4-7: 7(12,4)→beyond MB
    false, false, false, true,   // scan 8-11: 11(4,12)→TR=scan12
    false, true,  false, true,   // scan 12-15: 13(12,8), 15(12,12)→beyond MB
};

// ── Coded block pattern mapping — ITU-T H.264 Table 9-4 ─────────────────

/// CBP mapping table: [code_num][0=intra, 1=inter] → coded_block_pattern.
/// code_num is decoded via ue(v). Bits [3:0]=luma, [5:4]=chroma.
inline constexpr std::array<std::array<uint8_t, 2>, 48> cCbpTable = {{
    {{ 47,  0}}, {{  31,  16}}, {{ 15,  1}}, {{  0,  2}},
    {{ 23,  4}}, {{ 27,  8}}, {{ 29, 32}}, {{ 30,  3}},
    {{  7,  5}}, {{ 11,  10}}, {{ 13, 12}}, {{ 14, 15}},
    {{ 39,  47}}, {{ 43, 7}}, {{ 45, 11}}, {{ 46, 13}},
    {{ 16,  14}}, {{  3,  6}}, {{  5,  9}}, {{ 10, 31}},
    {{ 12,  35}}, {{ 19, 37}}, {{ 21, 42}}, {{ 26, 44}},
    {{ 28,  33}}, {{ 35, 34}}, {{ 37, 36}}, {{ 42, 40}},
    {{ 44,  39}}, {{  1, 43}}, {{  2, 45}}, {{  4, 46}},
    {{  8,  17}}, {{ 17, 18}}, {{ 18, 20}}, {{ 20, 24}},
    {{ 24,  19}}, {{  6, 21}}, {{  9, 26}}, {{ 22, 28}},
    {{ 25,  23}}, {{ 32, 27}}, {{ 33, 29}}, {{ 34, 30}},
    {{ 36,  22}}, {{ 40, 25}}, {{ 38, 38}}, {{ 41, 41}},
}};

// ── CAVLC suffix length thresholds — ITU-T H.264 §9.2.1 ────────────────

/// Thresholds for suffix_length adaptation during level decoding.
/// When |level| > threshold[suffix_len], increment suffix_len.
inline constexpr std::array<uint32_t, 7> cLevelSuffixThreshold = {
    0U, 3U, 6U, 12U, 24U, 48U, 0xFFFFFFFFU,
};

// ── CAVLC total_zeros tables — ITU-T H.264 Table 9-7/9-8 ───────────────

/// Maximum VLC code length for total_zeros lookup.
inline constexpr uint32_t cMaxTotalZerosVlcLen = 9U;

/// Total zeros VLC: [total_coeff-1][code] → {total_zeros, code_length}.
/// Packed as (total_zeros << 4) | code_length.
/// For total_coeff=1..15, decode total_zeros from the bitstream.
/// These are direct lookup tables indexed by peek bits.

// Rather than storing the full VLC tables (which are large and complex),
// we implement total_zeros decoding algorithmically using the spec tables.
// See cavlc.hpp for the implementation.

// ── CAVLC run_before tables — ITU-T H.264 Table 9-10 ───────────────────

// run_before is also decoded algorithmically in cavlc.hpp.

// ── Intra prediction mode mapping — ITU-T H.264 Table 8-2 ──────────────

/// Number of MB partitions per P-slice mb_type — ITU-T H.264 Table 7-13.
inline constexpr std::array<uint8_t, 6> cNumMbPartP = {
    1U, 2U, 2U, 4U, 1U, 1U,  // P_L0_16x16, P_L0_L0_16x8, P_L0_L0_8x16, P_8x8, P_8x8ref0, I_4x4
};

/// MB partition width for P-slice — ITU-T H.264 Table 7-13.
inline constexpr std::array<uint8_t, 4> cMbPartWidthP = {
    16U, 16U, 8U, 8U, // 16x16, 16x8, 8x16, 8x8
};

/// MB partition height for P-slice — ITU-T H.264 Table 7-13.
inline constexpr std::array<uint8_t, 4> cMbPartHeightP = {
    16U, 8U, 16U, 8U, // 16x16, 16x8, 8x16, 8x8
};

/// Sub-MB partition count per sub_mb_type — ITU-T H.264 Table 7-17.
inline constexpr std::array<uint8_t, 4> cNumSubMbPartP = {
    1U, 2U, 2U, 4U, // 8x8, 8x4, 4x8, 4x4
};

// ── QP to chroma QP mapping — ITU-T H.264 Table 8-15 ───────────────────

/// Maps luma QP (0-51) to chroma QP — ITU-T H.264 Table 8-15.
inline constexpr std::array<uint8_t, 52> cChromaQpTable = {
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 29, 30,
    31, 32, 32, 33, 34, 34, 35, 35, 36, 36, 37, 37, 37, 38, 38, 38,
    39, 39, 39, 39,
};

} // namespace sub0h264

#endif // CROG_SUB0H264_TABLES_HPP
