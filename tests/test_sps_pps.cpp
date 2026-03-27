#include "doctest.h"
#include "../components/sub0h264/src/annexb.hpp"
#include "../components/sub0h264/src/sps.hpp"
#include "../components/sub0h264/src/pps.hpp"
#include "../components/sub0h264/src/param_sets.hpp"

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

/** Helper: find and parse the first SPS NAL from a file. */
static bool findAndParseSps(const char* path, Sps& sps)
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
        if (nal.type != NalType::Sps)
            continue;

        BitReader br(nal.rbspData.data(), static_cast<uint32_t>(nal.rbspData.size()));
        return parseSps(br, sps) == Result::Ok;
    }
    return false;
}

/** Helper: parse SPS + PPS from a file into ParamSets. */
static bool parseParamSets(const char* path, ParamSets& ps)
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
                ps.storeSps(sps);
        }
        else if (nal.type == NalType::Pps)
        {
            Pps pps;
            if (parsePps(br, ps.spsArray(), pps) == Result::Ok)
                ps.storePps(pps);
        }
    }
    return true;
}

TEST_CASE("SPS: flat_black_640x480 — Baseline profile")
{
    Sps sps;
    REQUIRE(findAndParseSps(SUB0H264_TEST_FIXTURES_DIR "/flat_black_640x480.h264", sps));

    CHECK(sps.valid_);
    CHECK(sps.profileIdc_ == cProfileBaseline);
    CHECK(sps.width() == 640U);
    CHECK(sps.height() == 480U);
    CHECK(sps.widthInMbs_ == 40U);   // 640 / 16
    CHECK(sps.heightInMbs_ == 30U);  // 480 / 16
    CHECK(sps.frameMbsOnly_ == 1U);
    CHECK(sps.numRefFrames_ <= cMaxRefFrames);

    MESSAGE("SPS: profile=" << (int)sps.profileIdc_
            << " level=" << (int)sps.levelIdc_
            << " " << sps.width() << "x" << sps.height()
            << " refs=" << (int)sps.numRefFrames_
            << " poc_type=" << (int)sps.picOrderCntType_);
}

TEST_CASE("SPS: baseline_640x480_short — Baseline profile")
{
    Sps sps;
    REQUIRE(findAndParseSps(SUB0H264_TEST_FIXTURES_DIR "/baseline_640x480_short.h264", sps));

    CHECK(sps.valid_);
    CHECK(sps.profileIdc_ == cProfileBaseline);
    CHECK(sps.width() == 640U);
    CHECK(sps.height() == 480U);
}

TEST_CASE("PPS: flat_black_640x480 — parse PPS after SPS")
{
    ParamSets ps;
    REQUIRE(parseParamSets(SUB0H264_TEST_FIXTURES_DIR "/flat_black_640x480.h264", ps));

    // Should have at least one SPS and one PPS
    const Sps* sps = ps.getSps(0);
    REQUIRE(sps != nullptr);
    CHECK(sps->valid_);

    // Find the first valid PPS
    const Pps* pps = nullptr;
    for (uint8_t i = 0U; i < 255U; ++i)
    {
        pps = ps.getPps(i);
        if (pps)
            break;
    }
    REQUIRE(pps != nullptr);
    CHECK(pps->valid_);

    // Baseline uses CAVLC
    CHECK(pps->entropyCodingMode_ == 0U);
    CHECK_FALSE(pps->isCabac());

    MESSAGE("PPS: id=" << (int)pps->ppsId_
            << " sps_id=" << (int)pps->spsId_
            << " entropy=" << (pps->isCabac() ? "CABAC" : "CAVLC")
            << " qp=" << (int)pps->picInitQp_
            << " deblock=" << (int)pps->deblockingFilterControlPresent_);
}

TEST_CASE("PPS: baseline_640x480_short — CAVLC entropy")
{
    ParamSets ps;
    REQUIRE(parseParamSets(SUB0H264_TEST_FIXTURES_DIR "/baseline_640x480_short.h264", ps));

    const Pps* pps = nullptr;
    for (uint8_t i = 0U; i < 255U; ++i)
    {
        pps = ps.getPps(i);
        if (pps) break;
    }
    REQUIRE(pps != nullptr);
    CHECK_FALSE(pps->isCabac());
}

TEST_CASE("ParamSets: invalid SPS ID rejected")
{
    Sps sps;
    sps.spsId_ = 32U; // Out of range
    sps.valid_ = true;

    ParamSets ps;
    CHECK(ps.storeSps(sps) == Result::ErrorInvalidParam);
}

TEST_CASE("ParamSets: PPS with missing SPS rejected")
{
    // Create an RBSP for a minimal PPS: pps_id=0, sps_id=5 (doesn't exist)
    // ue(0)=1, ue(5)=00110 → binary: 1_00110 = 0b1001100 = 0x4C shifted
    const uint8_t rbsp[] = { 0b10011000, 0x00 }; // ue(0) + ue(5) + ...

    BitReader br(rbsp, sizeof(rbsp));
    ParamSets ps; // No SPS stored
    Pps pps;
    CHECK(parsePps(br, ps.spsArray(), pps) == Result::ErrorInvalidParam);
}
