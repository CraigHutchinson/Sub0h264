#include "doctest.h"
#include "../components/sub0h264/src/annexb.hpp"
#include "../components/sub0h264/src/sps.hpp"
#include "../components/sub0h264/src/pps.hpp"
#include "../components/sub0h264/src/param_sets.hpp"
#include "../components/sub0h264/src/slice.hpp"

#include <fstream>
#include <vector>

using namespace sub0h264;

static std::vector<uint8_t> loadFile(const char* path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open())
        return {};
    auto size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

/** Parse all NALs from a file, populate ParamSets, and collect slice headers. */
struct ParsedStream
{
    ParamSets paramSets;
    std::vector<SliceHeader> slices;
    std::vector<NalType> sliceNalTypes;

    bool parse(const char* path)
    {
        auto data = loadFile(path);
        if (data.empty())
            return false;

        std::vector<NalBounds> bounds;
        findNalUnits(data.data(), static_cast<uint32_t>(data.size()), bounds);

        for (const auto& b : bounds)
        {
            NalUnit nal;
            if (!parseNalUnit(data.data() + b.offset, b.size, nal))
                continue;

            BitReader br(nal.rbspData.data(), static_cast<uint32_t>(nal.rbspData.size()));

            if (nal.type == NalType::Sps)
            {
                Sps sps;
                if (parseSps(br, sps) == Result::Ok)
                    paramSets.storeSps(sps);
            }
            else if (nal.type == NalType::Pps)
            {
                Pps pps;
                if (parsePps(br, paramSets.spsArray(), pps) == Result::Ok)
                    paramSets.storePps(pps);
            }
            else if (isSliceNal(nal.type))
            {
                // Need PPS to parse slice header
                // Peek at pps_id: first_mb_in_slice(ue) + slice_type(ue) + pps_id(ue)
                BitReader peekBr(nal.rbspData.data(),
                                 static_cast<uint32_t>(nal.rbspData.size()));
                peekBr.readUev(); // first_mb_in_slice
                peekBr.readUev(); // slice_type
                uint8_t ppsId = static_cast<uint8_t>(peekBr.readUev());

                const Pps* pps = paramSets.getPps(ppsId);
                if (!pps)
                    continue;
                const Sps* sps = paramSets.getSps(pps->spsId_);
                if (!sps)
                    continue;

                bool isIdr = (nal.type == NalType::SliceIdr);
                SliceHeader sh;
                if (parseSliceHeader(br, *sps, *pps, isIdr, nal.refIdc, sh) == Result::Ok)
                {
                    slices.push_back(sh);
                    sliceNalTypes.push_back(nal.type);
                }
            }
        }
        return true;
    }
};

TEST_CASE("Slice: flat_black_640x480 — single IDR I-slice")
{
    ParsedStream stream;
    REQUIRE(stream.parse(SUB0H264_TEST_FIXTURES_DIR "/flat_black_640x480.h264"));

    REQUIRE(stream.slices.size() >= 1U);

    const auto& sh = stream.slices[0];
    CHECK(sh.isIdr_);
    CHECK(sh.sliceType_ == SliceType::I);
    CHECK(sh.firstMbInSlice_ == 0U);
    CHECK(sh.frameNum_ == 0U);

    MESSAGE("IDR slice: type=I qp_delta=" << sh.sliceQpDelta_
            << " deblock=" << (int)sh.disableDeblockingFilter_);
}

TEST_CASE("Slice: baseline_640x480_short — IDR + P-frames")
{
    ParsedStream stream;
    REQUIRE(stream.parse(SUB0H264_TEST_FIXTURES_DIR "/baseline_640x480_short.h264"));

    REQUIRE(stream.slices.size() >= 3U);

    // First slice should be IDR
    CHECK(stream.slices[0].isIdr_);
    CHECK(stream.slices[0].sliceType_ == SliceType::I);
    CHECK(stream.slices[0].firstMbInSlice_ == 0U);

    // Subsequent slices should be P-frames
    uint32_t iCount = 0U, pCount = 0U;
    for (const auto& sh : stream.slices)
    {
        if (sh.sliceType_ == SliceType::I)
            ++iCount;
        else if (sh.sliceType_ == SliceType::P)
            ++pCount;

        // All slices should start at MB 0 (single slice per frame)
        CHECK(sh.firstMbInSlice_ == 0U);
    }

    CHECK(iCount >= 1U);
    CHECK(pCount >= 2U);

    MESSAGE("Slices parsed: " << stream.slices.size()
            << " (I=" << iCount << " P=" << pCount << ")");
}

TEST_CASE("Slice: frame numbers start at 0 for IDR")
{
    ParsedStream stream;
    REQUIRE(stream.parse(SUB0H264_TEST_FIXTURES_DIR "/baseline_640x480_short.h264"));
    REQUIRE(stream.slices.size() >= 3U);

    // IDR resets frame_num to 0
    CHECK(stream.slices[0].frameNum_ == 0U);

    // Frame numbers increment modulo maxFrameNum (may wrap back to 0)
    // Just verify they are within valid range
    const Sps* sps = stream.paramSets.getSps(0);
    REQUIRE(sps != nullptr);
    uint16_t maxFrameNum = static_cast<uint16_t>(sps->maxFrameNum());

    for (const auto& sh : stream.slices)
        CHECK(sh.frameNum_ < maxFrameNum);
}

TEST_CASE("Slice: QP delta is within valid range")
{
    ParsedStream stream;
    REQUIRE(stream.parse(SUB0H264_TEST_FIXTURES_DIR "/baseline_640x480_short.h264"));

    for (const auto& sh : stream.slices)
    {
        // slice_qp_delta should result in a valid QP (0-51)
        // QP = picInitQp + sliceQpDelta, picInitQp is typically 26
        // So sliceQpDelta should be in [-26, 25]
        CHECK(sh.sliceQpDelta_ >= -26);
        CHECK(sh.sliceQpDelta_ <= 25);
    }
}
