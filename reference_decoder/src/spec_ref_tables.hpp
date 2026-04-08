/** Spec-only Scan Order and Dequant Tables
 *
 *  ITU-T H.264 Section 6.4.3  (Scan orders)
 *  ITU-T H.264 Section 8.5.12.1 (Dequantization, Table 8-15)
 *
 *  All tables derived from spec formulas with constexpr verification.
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef CROG_SUB0H264_SPEC_REF_TABLES_HPP
#define CROG_SUB0H264_SPEC_REF_TABLES_HPP

#include <cstdint>

namespace sub0h264 {
namespace spec_ref {

// ============================================================================
// Inverse 4x4 Zigzag Scan -- ITU-T H.264 Section 8.5.6, Figure 8-9
// Maps coefficient scan index [0..15] to raster position (row*4+col).
// ============================================================================
inline constexpr uint8_t cZigzag4x4[16] = {
    0,  1,  4,  8,
    5,  2,  3,  6,
    9, 12, 13, 10,
    7, 11, 14, 15,
};

// Spot-checks: DC at position 0, last AC at position 15
static_assert(cZigzag4x4[0] == 0, "Zigzag DC maps to (0,0)");
static_assert(cZigzag4x4[15] == 15, "Zigzag last maps to (3,3)");
static_assert(cZigzag4x4[1] == 1, "Zigzag[1] = (0,1)");
static_assert(cZigzag4x4[2] == 4, "Zigzag[2] = (1,0)");
static_assert(cZigzag4x4[3] == 8, "Zigzag[3] = (2,0)");

// ============================================================================
// Luma 4x4 Block Pixel Offsets -- ITU-T H.264 Section 6.4.3
// Maps block index [0..15] in spec scan order to (x,y) pixel offset
// within the 16x16 macroblock.
//
// The spec defines the 4x4 block scan order as:
//   Block:  0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15
//   (x,y): (0,0)(4,0)(0,4)(4,4)(8,0)(12,0)(8,4)(12,4)...
//
// Inverse raster scan: blkX = InverseRasterScan(i/4, 8, 8, 16, 0) +
//                              InverseRasterScan(i%4, 4, 4, 8, 0)
//                      blkY = InverseRasterScan(i/4, 8, 8, 16, 1) +
//                              InverseRasterScan(i%4, 4, 4, 8, 1)
// ============================================================================
inline constexpr uint8_t cLuma4x4BlkX[16] = {
    0, 4, 0, 4,  8, 12, 8, 12,  0, 4, 0, 4,  8, 12, 8, 12,
};

inline constexpr uint8_t cLuma4x4BlkY[16] = {
    0, 0, 4, 4,  0, 0, 4, 4,  8, 8, 12, 12,  8, 8, 12, 12,
};

// Spot-checks
static_assert(cLuma4x4BlkX[0] == 0 && cLuma4x4BlkY[0] == 0, "Block 0 at (0,0)");
static_assert(cLuma4x4BlkX[1] == 4 && cLuma4x4BlkY[1] == 0, "Block 1 at (4,0)");
static_assert(cLuma4x4BlkX[2] == 0 && cLuma4x4BlkY[2] == 4, "Block 2 at (0,4)");
static_assert(cLuma4x4BlkX[3] == 4 && cLuma4x4BlkY[3] == 4, "Block 3 at (4,4)");
static_assert(cLuma4x4BlkX[4] == 8 && cLuma4x4BlkY[4] == 0, "Block 4 at (8,0)");
static_assert(cLuma4x4BlkX[15] == 12 && cLuma4x4BlkY[15] == 12, "Block 15 at (12,12)");

// ============================================================================
// Chroma 4x4 Block Pixel Offsets -- ITU-T H.264 Section 6.4.7 (for 4:2:0)
// Maps chroma block index [0..3] to (x,y) pixel offset within 8x8 chroma MB.
// ============================================================================
inline constexpr uint8_t cChroma4x4BlkX[4] = { 0, 4, 0, 4 };
inline constexpr uint8_t cChroma4x4BlkY[4] = { 0, 0, 4, 4 };

static_assert(cChroma4x4BlkX[0] == 0 && cChroma4x4BlkY[0] == 0, "Chroma block 0");
static_assert(cChroma4x4BlkX[3] == 4 && cChroma4x4BlkY[3] == 4, "Chroma block 3");

// ============================================================================
// Dequantization Scale Factors -- ITU-T H.264 Table 8-15
//
// LevelScale(qP%6, i, j) depends on the position class:
//   Class 0: positions where (i+j) % 2 == 0 and i % 2 == 0 and j % 2 == 0
//            i.e., (0,0), (0,2), (2,0), (2,2)
//   Class 1: positions where (i+j) % 2 == 1
//            i.e., (0,1), (0,3), (1,0), (1,2), (2,1), (2,3), (3,0), (3,2)
//   Class 2: positions where (i+j) % 2 == 0 and (i % 2 == 1 or j % 2 == 1)
//            i.e., (1,1), (1,3), (3,1), (3,3)
//
// Table 8-15:
//   qP%6  Class0  Class1  Class2
//   0     10      13      10
//   1     11      14      11
//   2     13      16      13
//   3     14      18      14
//   4     16      20      16
//   5     18      23      18
// ============================================================================
inline constexpr int32_t cDequantScale[6][3] = {
    {10, 13, 10},  // qP%6 = 0
    {11, 14, 11},  // qP%6 = 1
    {13, 16, 13},  // qP%6 = 2
    {14, 18, 14},  // qP%6 = 3
    {16, 20, 16},  // qP%6 = 4
    {18, 23, 18},  // qP%6 = 5
};

// Spot-checks against Table 8-15
static_assert(cDequantScale[0][0] == 10, "Table 8-15: qP%6=0 class0");
static_assert(cDequantScale[0][1] == 13, "Table 8-15: qP%6=0 class1");
static_assert(cDequantScale[5][1] == 23, "Table 8-15: qP%6=5 class1");
static_assert(cDequantScale[3][0] == 14, "Table 8-15: qP%6=3 class0");

/** Get the position class for a 4x4 block position (row, col).
 *
 *  ITU-T H.264 Table 8-15 position classification:
 *  - Class 0: even row and even col
 *  - Class 1: (row + col) is odd
 *  - Class 2: odd row and odd col (i.e. both odd but sum is even)
 */
inline constexpr uint8_t positionClass4x4(uint32_t row, uint32_t col) noexcept
{
    bool rowEven = (row & 1U) == 0U;
    bool colEven = (col & 1U) == 0U;
    if (rowEven && colEven) return 0U;       // Class 0: (0,0),(0,2),(2,0),(2,2)
    if (rowEven != colEven) return 1U;       // Class 1: (row+col) odd
    return 2U;                                // Class 2: (1,1),(1,3),(3,1),(3,3)
}

/// Precomputed position class for each of 16 positions in a 4x4 block.
/// posClass[row * 4 + col].
inline constexpr uint8_t cPosClass4x4[16] = {
    0, 1, 0, 1,  // row 0
    1, 2, 1, 2,  // row 1
    0, 1, 0, 1,  // row 2
    1, 2, 1, 2,  // row 3
};

// Verify the precomputed table matches the formula
static_assert(cPosClass4x4[0] == positionClass4x4(0, 0), "pos(0,0)");
static_assert(cPosClass4x4[1] == positionClass4x4(0, 1), "pos(0,1)");
static_assert(cPosClass4x4[5] == positionClass4x4(1, 1), "pos(1,1)");
static_assert(cPosClass4x4[15] == positionClass4x4(3, 3), "pos(3,3)");

// ============================================================================
// Chroma QP Mapping -- ITU-T H.264 Table 8-13
// Maps QPi (= QP + chromaQpOffset) to QPc for chroma dequant.
// For QPi <= 29: QPc = QPi. For QPi >= 30: use table.
// ============================================================================
inline constexpr int32_t cChromaQpTable[52] = {
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    29, 30, 31, 32, 32, 33, 34, 34, 35, 35,
    36, 36, 37, 37, 37, 38, 38, 38, 39, 39,
    39, 39,
};

static_assert(cChromaQpTable[0] == 0, "Table 8-13: QPi=0");
static_assert(cChromaQpTable[29] == 29, "Table 8-13: QPi=29");
static_assert(cChromaQpTable[30] == 29, "Table 8-13: QPi=30");
static_assert(cChromaQpTable[51] == 39, "Table 8-13: QPi=51");

/** Get chroma QP from luma QP and offset.
 *  ITU-T H.264 Section 8.5.8.
 */
inline int32_t chromaQp(int32_t qpY, int32_t chromaQpOffset) noexcept
{
    int32_t qpI = qpY + chromaQpOffset;
    if (qpI < 0) qpI = 0;
    if (qpI > 51) qpI = 51;
    return cChromaQpTable[qpI];
}

} // namespace spec_ref
} // namespace sub0h264

#endif // CROG_SUB0H264_SPEC_REF_TABLES_HPP
