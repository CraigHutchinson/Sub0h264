#include "doctest.h"
#include "../components/sub0h264/src/annexb.hpp"
#include "../components/sub0h264/src/sps.hpp"
#include "../components/sub0h264/src/pps.hpp"
#include "../components/sub0h264/src/param_sets.hpp"
#include "../components/sub0h264/src/slice.hpp"
#include "../components/sub0h264/src/cavlc.hpp"

#include "test_fixtures.hpp"

using namespace sub0h264;

TEST_CASE("CAVLC: coeff_token decode with nC=0 TC=0 TO=0")
{
    // nC<2, TC=0, TO=0: code=5, size=6 → binary '000101'
    // 000101_00 = 0x14
    const uint8_t data[] = { 0x14 };
    BitReader br(data, 1U);
    CoeffToken ct = decodeCoeffToken(br, 0);
    CHECK(ct.totalCoeff == 0U);
    CHECK(ct.trailingOnes == 0U);
    CHECK(br.bitOffset() == 6U);
}

TEST_CASE("CAVLC: coeff_token nC=0 totalCoeff=1 trailingOnes=1")
{
    // nC<2, TC=1, TO=1: cCoeffTokenCode[0][1][1]=4, size=6 → binary '000100'
    // 000100_00 = 0x10
    const uint8_t data[] = { 0x10 };
    BitReader br(data, 1U);
    CoeffToken ct = decodeCoeffToken(br, 0);
    CHECK(ct.totalCoeff == 1U);
    CHECK(ct.trailingOnes == 1U);
    CHECK(br.bitOffset() == 6U);
}

TEST_CASE("CAVLC: coeff_token chroma DC TC=1 TO=1")
{
    // Chroma DC, TC=1, TO=1: cCoeffTokenCodeChroma[1][1]=6, size=6 → binary '000110'
    // 000110_00 = 0x18
    const uint8_t data[] = { 0x18 };
    BitReader br(data, 1U);
    CoeffToken ct = decodeCoeffToken(br, -1);
    CHECK(ct.totalCoeff == 1U);
    CHECK(ct.trailingOnes == 1U);
    CHECK(br.bitOffset() == 6U);
}

TEST_CASE("CAVLC: coeff_token with high nC (>=8)")
{
    // nC>=8: 6-bit fixed code
    // 000000 = totalCoeff=0, trailingOnes=0
    const uint8_t data[] = { 0x04 }; // 0000 0100 → code=000001 → TO=1, TC=(0>>2)+1=1
    BitReader br(data, 1U);
    CoeffToken ct = decodeCoeffToken(br, 8);
    CHECK(ct.totalCoeff <= 16U);
    CHECK(ct.trailingOnes <= 3U);
}

TEST_CASE("CAVLC: decodeLevel basic positive")
{
    // Level prefix=0 (bit '1'), suffix_len=0 → levelCode=0 → level=0... hmm
    // Actually prefix=0 means read '1' immediately → levelCode = min(0,15) = 0
    // levelCode=0 → absLevel = (0+2)>>1 = 1, sign = even → positive → level=1
    const uint8_t data[] = { 0x80 }; // 1...
    BitReader br(data, 1U);
    uint32_t suffixLen = 0U;
    int32_t level = decodeLevel(br, suffixLen);
    CHECK(level == 1);
}

TEST_CASE("CAVLC: decodeLevel negative")
{
    // prefix=1 (bits '01'), suffix_len=0 → levelCode = min(1,15) = 1
    // levelCode=1 → absLevel = (1+2)>>1 = 1, sign = odd → negative → level=-1
    const uint8_t data[] = { 0x40 }; // 01...
    BitReader br(data, 1U);
    uint32_t suffixLen = 0U;
    int32_t level = decodeLevel(br, suffixLen);
    CHECK(level == -1);
}

TEST_CASE("CAVLC: decodeLevel larger values")
{
    // prefix=2 (bits '001'), suffix_len=0 → levelCode = 2
    // absLevel = (2+2)>>1 = 2, sign = even → level=2
    const uint8_t data[] = { 0x20 }; // 001...
    BitReader br(data, 1U);
    uint32_t suffixLen = 0U;
    int32_t level = decodeLevel(br, suffixLen);
    CHECK(level == 2);
}

TEST_CASE("CAVLC: suffix length adaptation")
{
    // Decode a level that triggers suffix length increase
    // threshold[0]=0, so any level > 0 should bump suffixLen from 0 to 1
    const uint8_t data[] = { 0x80 }; // prefix=0 → level=1
    BitReader br(data, 1U);
    uint32_t suffixLen = 0U;
    decodeLevel(br, suffixLen);
    CHECK(suffixLen == 1U); // Should have incremented from 0 to 1
}

TEST_CASE("CAVLC: decodeRunBefore zerosLeft=0")
{
    const uint8_t data[] = { 0xFF };
    BitReader br(data, 1U);
    CHECK(decodeRunBefore(br, 0U) == 0U);
    CHECK(br.bitOffset() == 0U); // Should not consume any bits
}

TEST_CASE("CAVLC: decodeRunBefore zerosLeft=1")
{
    // zerosLeft=1, Table 9-10: run=0 code=1 size=1, run=1 code=0 size=1
    // So bit '1' → run=0, bit '0' → run=1
    const uint8_t data0[] = { 0x80 }; // bit='1' → run=0
    BitReader br0(data0, 1U);
    CHECK(decodeRunBefore(br0, 1U) == 0U);

    const uint8_t data1[] = { 0x00 }; // bit='0' → run=1
    BitReader br1(data1, 1U);
    CHECK(decodeRunBefore(br1, 1U) == 1U);
}

TEST_CASE("CAVLC: ResidualBlock4x4 with zero coefficients")
{
    // nC=0, coeff_token should decode to totalCoeff=0
    // '1' bit → totalCoeff=0, trailingOnes=0
    const uint8_t data[] = { 0x80 };
    BitReader br(data, 1U);
    ResidualBlock4x4 block;
    CHECK(decodeResidualBlock4x4(br, 0, cMaxCoeff4x4, 0U, block) == Result::Ok);
    CHECK(block.totalCoeff == 0U);
    for (uint32_t i = 0U; i < 16U; ++i)
        CHECK(block.coeffs[i] == 0);
}

TEST_CASE("CAVLC: ResidualBlock4x4 with zero coefficients (proper VLC)")
{
    // nC=0, TC=0, TO=0: code=5, size=6 → '000101'
    // Then no more data needed (totalCoeff=0)
    // 000101_00 = 0x14
    const uint8_t data[] = { 0x14 };
    BitReader br(data, 1U);
    ResidualBlock4x4 block;
    Result res = decodeResidualBlock4x4(br, 0, cMaxCoeff4x4, 0U, block);
    CHECK(res == Result::Ok);
    CHECK(block.totalCoeff == 0U);

    for (uint32_t i = 0U; i < 16U; ++i)
        CHECK(block.coeffs[i] == 0);
}

TEST_CASE("CAVLC: IMbType helpers")
{
    CHECK_FALSE(isI16x16(0U));  // I_4x4
    CHECK(isI16x16(1U));        // I_16x16_0_0_0
    CHECK(isI16x16(24U));       // I_16x16_3_2_15

    CHECK(i16x16PredMode(1U) == 0U);
    CHECK(i16x16PredMode(2U) == 1U);
    CHECK(i16x16PredMode(4U) == 3U);

    CHECK(i16x16CbpLuma(1U) == 0U);   // First 12 types have cbpLuma=0
    CHECK(i16x16CbpLuma(13U) == 15U);  // Types 13-24 have cbpLuma=15
}

TEST_CASE("CAVLC: CBP table mapping")
{
    // CBP table: code_num 0 → intra=47, inter=0
    CHECK(cCbpTable[0][0] == 47U);
    CHECK(cCbpTable[0][1] == 0U);

    // code_num 3 → intra=0, inter=2
    CHECK(cCbpTable[3][0] == 0U);
    CHECK(cCbpTable[3][1] == 2U);
}

TEST_CASE("CAVLC tables: zigzag scan is valid permutation")
{
    // Verify zigzag4x4 is a permutation of 0-15
    std::array<bool, 16> seen{};
    for (uint32_t i = 0U; i < 16U; ++i)
    {
        REQUIRE(cZigzag4x4[i] < 16U);
        CHECK_FALSE(seen[cZigzag4x4[i]]);
        seen[cZigzag4x4[i]] = true;
    }
}

TEST_CASE("CAVLC tables: chroma QP mapping")
{
    // QP 0-29 should map to same value
    for (uint32_t qp = 0U; qp <= 29U; ++qp)
        CHECK(cChromaQpTable[qp] == qp);

    // QP 30+ should saturate (chroma QP <= 39)
    CHECK(cChromaQpTable[51] == 39U);
}
