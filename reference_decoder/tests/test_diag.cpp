/** Spec-only Reference Decoder -- Diagnostic Trace
 *
 *  Decodes fixtures and prints per-MB diagnostic information
 *  for debugging purposes. Not a normal unit test.
 *
 *  SPDX-License-Identifier: MIT
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "../src/spec_ref_cabac.hpp"
#include "../src/spec_ref_cabac_init.hpp"
#include "../src/spec_ref_cabac_parse.hpp"
#include "../src/spec_ref_tables.hpp"
#include "../src/spec_ref_transform.hpp"
#include "../src/spec_ref_intra_pred.hpp"
#include "../src/spec_ref_decode.hpp"

#include "../../tests/test_fixtures.hpp"
#include "../../components/sub0h264/src/bitstream.hpp"
#include "../../components/sub0h264/src/frame.hpp"
#include "../../components/sub0h264/src/sps.hpp"
#include "../../components/sub0h264/src/pps.hpp"
#include "../../components/sub0h264/src/slice.hpp"
#include "../../components/sub0h264/src/annexb.hpp"

#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>
#include <memory>

using namespace sub0h264;
using namespace sub0h264::spec_ref;

TEST_CASE("Diagnostic: trace cabac_flat_main")
{
    auto h264Data = getFixture("cabac_flat_main.h264");
    if (h264Data.empty()) { MESSAGE("Fixture not available"); return; }

    std::vector<NalBounds> nalBounds;
    findNalUnits(h264Data.data(), static_cast<uint32_t>(h264Data.size()), nalBounds);

    Sps spsArray[cMaxSpsCount] = {};
    Pps ppsArray[cMaxPpsCount] = {};

    for (const auto& bounds : nalBounds) {
        NalUnit nal;
        if (!parseNalUnit(h264Data.data() + bounds.offset, bounds.size, nal))
            continue;
        if (nal.type == NalType::Sps) {
            BitReader br(nal.rbspData.data(), static_cast<uint32_t>(nal.rbspData.size()));
            Sps sps;
            if (parseSps(br, sps) == Result::Ok)
                spsArray[sps.spsId_] = sps;
        } else if (nal.type == NalType::Pps) {
            BitReader br(nal.rbspData.data(), static_cast<uint32_t>(nal.rbspData.size()));
            Pps pps;
            if (parsePps(br, spsArray, pps) == Result::Ok) {
                ppsArray[pps.ppsId_] = pps;
                MESSAGE("PPS: entropyCodingMode=" << (int)pps.entropyCodingMode_
                        << " picInitQp=" << (int)pps.picInitQp_
                        << " chromaQpOffset=" << (int)pps.chromaQpIndexOffset_);
            }
        }
    }

    for (const auto& bounds : nalBounds) {
        NalUnit nal;
        if (!parseNalUnit(h264Data.data() + bounds.offset, bounds.size, nal))
            continue;
        if (nal.type != NalType::SliceIdr)
            continue;

        BitReader br(nal.rbspData.data(), static_cast<uint32_t>(nal.rbspData.size()));
        BitReader brPeek = br;
        brPeek.readUev();
        brPeek.readUev();
        uint32_t ppsId = brPeek.readUev();

        const Pps* pps = &ppsArray[ppsId];
        const Sps* sps = &spsArray[pps->spsId_];

        SliceHeader sh;
        parseSliceHeader(br, *sps, *pps, true, nal.refIdc, sh);

        Frame frame;
        frame.allocate(sps->width(), sps->height());

        br.alignToByte();
        CabacEngine engine;
        engine.init(br);

        int32_t sliceQpY = pps->picInitQp_ + sh.sliceQpDelta_;
        auto contexts = std::make_unique<CabacCtx[]>(cNumCabacContexts);
        initCabacContexts(contexts.get(), sliceQpY);

        uint32_t widthInMbs = sps->widthInMbs_;
        uint32_t heightInMbs = sps->heightInMbs_;
        uint32_t totalMbs = widthInMbs * heightInMbs;
        auto mbInfoArray = std::make_unique<MbInfo[]>(totalMbs);
        int32_t currentQp = sliceQpY;

        MESSAGE("SliceQP=" << sliceQpY << " widthInMbs=" << widthInMbs
                << " heightInMbs=" << heightInMbs << " total=" << totalMbs);

        for (uint32_t mbAddr = 0U; mbAddr < totalMbs; ++mbAddr) {
            uint32_t mbX = mbAddr % widthInMbs;
            uint32_t mbY = mbAddr / widthInMbs;

            uint32_t ctxA = 0U, ctxB = 0U;
            if (mbX > 0U && mbInfoArray[mbAddr - 1U].available)
                ctxA = (mbInfoArray[mbAddr - 1U].type != MbTypeClass::I_4x4) ? 1U : 0U;
            if (mbY > 0U && mbInfoArray[(mbY - 1U) * widthInMbs + mbX].available)
                ctxB = (mbInfoArray[(mbY - 1U) * widthInMbs + mbX].type != MbTypeClass::I_4x4) ? 1U : 0U;

            uint32_t mbType = cabacDecodeMbTypeI(engine, contexts.get(), ctxA, ctxB);

            if (mbAddr < 10U || mbType > 24U) {
                const char* typeStr = "?";
                if (mbType == 0U) typeStr = "I_4x4";
                else if (mbType >= 1U && mbType <= 24U) typeStr = "I_16x16";
                else if (mbType == 25U) typeStr = "I_PCM";

                MESSAGE("MB(" << mbX << "," << mbY << ") addr=" << mbAddr
                        << " mbType=" << mbType << " [" << typeStr << "]"
                        << " range=" << engine.codIRange()
                        << " offset=" << engine.codIOffset());
            }

            if (mbType == 25U) {
                MESSAGE("I_PCM at MB " << mbAddr << " - stopping");
                break;
            }

            if (mbType == 0U) {
                decodeI4x4Mb(engine, contexts.get(), frame, mbInfoArray.get(),
                             widthInMbs, mbX, mbY, currentQp,
                             pps->chromaQpIndexOffset_);
            } else if (mbType >= 1U && mbType <= 24U) {
                uint32_t val = mbType - 1U;
                uint32_t i16x16PredMode = val % 4U;
                val /= 4U;
                uint32_t cbpChroma = val % 3U;
                uint32_t cbpLumaFlag = val / 3U;
                uint32_t cbpLuma = cbpLumaFlag ? 0x0FU : 0U;
                decodeI16x16Mb(engine, contexts.get(), frame, mbInfoArray.get(),
                               widthInMbs, mbX, mbY, currentQp,
                               i16x16PredMode, cbpLuma, cbpChroma,
                               pps->chromaQpIndexOffset_);
            }

            if (mbAddr < totalMbs - 1U) {
                uint32_t endFlag = engine.decodeTerminate();
                if (endFlag) {
                    MESSAGE("Early end of slice at MB " << mbAddr);
                    break;
                }
            }
        }
        break;
    }
}

TEST_CASE("Diagnostic: MB(0,0) coefficient trace for bouncing_ball_ionly_cabac")
{
    auto h264Data = getFixture("bouncing_ball_ionly_cabac.h264");
    if (h264Data.empty()) { MESSAGE("Fixture not available"); return; }

    std::vector<NalBounds> nalBounds;
    findNalUnits(h264Data.data(), static_cast<uint32_t>(h264Data.size()), nalBounds);

    Sps spsArray[cMaxSpsCount] = {};
    Pps ppsArray[cMaxPpsCount] = {};

    for (const auto& bounds : nalBounds) {
        NalUnit nal;
        if (!parseNalUnit(h264Data.data() + bounds.offset, bounds.size, nal))
            continue;
        if (nal.type == NalType::Sps) {
            BitReader br(nal.rbspData.data(), static_cast<uint32_t>(nal.rbspData.size()));
            Sps sps;
            if (parseSps(br, sps) == Result::Ok) spsArray[sps.spsId_] = sps;
        } else if (nal.type == NalType::Pps) {
            BitReader br(nal.rbspData.data(), static_cast<uint32_t>(nal.rbspData.size()));
            Pps pps;
            if (parsePps(br, spsArray, pps) == Result::Ok) ppsArray[pps.ppsId_] = pps;
        }
    }

    for (const auto& bounds : nalBounds) {
        NalUnit nal;
        if (!parseNalUnit(h264Data.data() + bounds.offset, bounds.size, nal))
            continue;
        if (nal.type != NalType::SliceIdr) continue;

        BitReader br(nal.rbspData.data(), static_cast<uint32_t>(nal.rbspData.size()));
        BitReader brPeek = br;
        brPeek.readUev();
        brPeek.readUev();
        uint32_t ppsId = brPeek.readUev();

        const Pps* pps = &ppsArray[ppsId];
        const Sps* sps = &spsArray[pps->spsId_];

        SliceHeader sh;
        parseSliceHeader(br, *sps, *pps, true, nal.refIdc, sh);

        Frame frame;
        frame.allocate(sps->width(), sps->height());

        br.alignToByte();
        CabacEngine engine;
        engine.init(br);

        int32_t sliceQpY = pps->picInitQp_ + sh.sliceQpDelta_;
        auto contexts = std::make_unique<CabacCtx[]>(cNumCabacContexts);
        initCabacContexts(contexts.get(), sliceQpY);

        uint32_t widthInMbs = sps->widthInMbs_;
        auto mbInfoArray = std::make_unique<MbInfo[]>(widthInMbs * sps->heightInMbs_);
        int32_t currentQp = sliceQpY;

        MESSAGE("SliceQP=" << sliceQpY);

        // Decode MB(0,0) only -- it's I_4x4
        uint32_t ctxA = 0U, ctxB = 0U;
        uint32_t mbType = cabacDecodeMbTypeI(engine, contexts.get(), ctxA, ctxB);
        MESSAGE("MB(0,0) mbType=" << mbType);

        if (mbType == 0U) {
            // Manually decode the first few prediction modes
            for (uint32_t blkIdx = 0U; blkIdx < 16U; ++blkIdx) {
                uint32_t prevFlag, remMode;
                cabacDecodeIntra4x4PredMode(engine, contexts.get(), prevFlag, remMode);
                if (blkIdx < 4U) {
                    MESSAGE("  Block " << blkIdx << " prevFlag=" << prevFlag << " remMode=" << remMode);
                }
            }

            // Chroma mode
            uint32_t chromaMode = cabacDecodeIntraChromaMode(engine, contexts.get(), 0, 0);
            MESSAGE("  chromaMode=" << chromaMode);

            // CBP
            uint32_t cbp = cabacDecodeCbp(engine, contexts.get(), 0, 0, false, false);
            MESSAGE("  CBP=" << cbp << " (luma=" << (cbp & 0xF)
                    << " chroma=" << ((cbp >> 4) & 3) << ")");

            // QP delta (only if CBP != 0)
            if (cbp != 0) {
                int32_t qpDelta = cabacDecodeMbQpDelta(engine, contexts.get(), false);
                MESSAGE("  qpDelta=" << qpDelta << " QP=" << (currentQp + qpDelta));
            }

            // Decode block 0 residual manually with trace
            uint32_t blk8x8Idx = 0; // block 0 is in 8x8 group 0
            if ((cbp >> blk8x8Idx) & 1U) {
                // CBF context for block 0: both neighbors unavailable -> condTerm=1 each
                // ctxIdxInc = 1 + 2*1 = 3
                uint32_t cbfInc = 3U; // Both MB unavailable for first block
                int16_t scanCoeffs[16] = {};
                bool hasCoded = cabacDecodeResidual4x4(engine, contexts.get(),
                                                        scanCoeffs, 16U, 2U, cbfInc);
                MESSAGE("  Block 0 coded=" << hasCoded);
                if (hasCoded) {
                    MESSAGE("  Scan coeffs: "
                            << scanCoeffs[0] << " " << scanCoeffs[1] << " "
                            << scanCoeffs[2] << " " << scanCoeffs[3] << " "
                            << scanCoeffs[4] << " " << scanCoeffs[5] << " "
                            << scanCoeffs[6] << " " << scanCoeffs[7]);

                    // Zigzag reorder
                    int16_t rasterCoeffs[16] = {};
                    for (uint32_t k = 0U; k < 16U; ++k)
                        rasterCoeffs[cZigzag4x4[k]] = scanCoeffs[k];

                    MESSAGE("  Raster DC=" << rasterCoeffs[0]
                            << " AC[1]=" << rasterCoeffs[1]
                            << " AC[4]=" << rasterCoeffs[4]
                            << " AC[5]=" << rasterCoeffs[5]);

                    // Dequant
                    inverseQuantize4x4(rasterCoeffs, currentQp);
                    MESSAGE("  Dequant DC=" << rasterCoeffs[0]
                            << " AC[1]=" << rasterCoeffs[1]
                            << " AC[4]=" << rasterCoeffs[4]);

                    // IDCT (without prediction)
                    inverseDct4x4(rasterCoeffs);
                    MESSAGE("  Residual: " << rasterCoeffs[0] << " " << rasterCoeffs[1]
                            << " " << rasterCoeffs[2] << " " << rasterCoeffs[3]);
                }
            }
        }

        break;
    }
}

TEST_CASE("Diagnostic: trace first 5 MBs of bouncing_ball_ionly_cabac")
{
    auto h264Data = getFixture("bouncing_ball_ionly_cabac.h264");
    if (h264Data.empty()) { MESSAGE("Fixture not available"); return; }

    // Manual decode with per-MB tracing
    std::vector<NalBounds> nalBounds;
    findNalUnits(h264Data.data(), static_cast<uint32_t>(h264Data.size()), nalBounds);

    Sps spsArray[cMaxSpsCount] = {};
    Pps ppsArray[cMaxPpsCount] = {};

    for (const auto& bounds : nalBounds) {
        NalUnit nal;
        if (!parseNalUnit(h264Data.data() + bounds.offset, bounds.size, nal))
            continue;
        if (nal.type == NalType::Sps) {
            BitReader br(nal.rbspData.data(), static_cast<uint32_t>(nal.rbspData.size()));
            Sps sps;
            if (parseSps(br, sps) == Result::Ok)
                spsArray[sps.spsId_] = sps;
        } else if (nal.type == NalType::Pps) {
            BitReader br(nal.rbspData.data(), static_cast<uint32_t>(nal.rbspData.size()));
            Pps pps;
            if (parsePps(br, spsArray, pps) == Result::Ok)
                ppsArray[pps.ppsId_] = pps;
        }
    }

    for (const auto& bounds : nalBounds) {
        NalUnit nal;
        if (!parseNalUnit(h264Data.data() + bounds.offset, bounds.size, nal))
            continue;
        if (nal.type != NalType::SliceIdr)
            continue;

        BitReader br(nal.rbspData.data(), static_cast<uint32_t>(nal.rbspData.size()));
        BitReader brPeek = br;
        brPeek.readUev();
        brPeek.readUev();
        uint32_t ppsId = brPeek.readUev();

        const Pps* pps = &ppsArray[ppsId];
        const Sps* sps = &spsArray[pps->spsId_];

        SliceHeader sh;
        parseSliceHeader(br, *sps, *pps, true, nal.refIdc, sh);

        Frame frame;
        frame.allocate(sps->width(), sps->height());

        br.alignToByte();
        CabacEngine engine;
        engine.init(br);

        int32_t sliceQpY = pps->picInitQp_ + sh.sliceQpDelta_;
        auto contexts = std::make_unique<CabacCtx[]>(cNumCabacContexts);
        initCabacContexts(contexts.get(), sliceQpY);

        uint32_t widthInMbs = sps->widthInMbs_;
        uint32_t heightInMbs = sps->heightInMbs_;
        uint32_t totalMbs = widthInMbs * heightInMbs;
        auto mbInfoArray = std::make_unique<MbInfo[]>(totalMbs);
        int32_t currentQp = sliceQpY;

        MESSAGE("SliceQP=" << sliceQpY << " widthInMbs=" << widthInMbs
                << " heightInMbs=" << heightInMbs);

        for (uint32_t mbAddr = 0U; mbAddr < totalMbs; ++mbAddr) {
            uint32_t mbX = mbAddr % widthInMbs;
            uint32_t mbY = mbAddr / widthInMbs;

            uint32_t ctxA = 0U, ctxB = 0U;
            if (mbX > 0U && mbInfoArray[mbAddr - 1U].available)
                ctxA = (mbInfoArray[mbAddr - 1U].type != MbTypeClass::I_4x4) ? 1U : 0U;
            if (mbY > 0U && mbInfoArray[(mbY - 1U) * widthInMbs + mbX].available)
                ctxB = (mbInfoArray[(mbY - 1U) * widthInMbs + mbX].type != MbTypeClass::I_4x4) ? 1U : 0U;

            uint32_t mbType = cabacDecodeMbTypeI(engine, contexts.get(), ctxA, ctxB);

            bool shouldTrace = (mbAddr < 5U) || (mbAddr >= 50U && mbAddr <= 56U);
            if (shouldTrace) {
                const char* typeStr = "?";
                if (mbType == 0U) typeStr = "I_4x4";
                else if (mbType >= 1U && mbType <= 24U) typeStr = "I_16x16";
                else if (mbType == 25U) typeStr = "I_PCM";

                uint32_t predMode = 0, cbpLuma = 0, cbpChroma = 0;
                if (mbType >= 1U && mbType <= 24U) {
                    uint32_t val = mbType - 1U;
                    predMode = val % 4U;
                    val /= 4U;
                    cbpChroma = val % 3U;
                    cbpLuma = (val / 3U) ? 0x0FU : 0U;
                }

                MESSAGE("MB(" << mbX << "," << mbY << ") addr=" << mbAddr
                        << " mbType=" << mbType
                        << " [" << typeStr << "]"
                        << " predMode=" << predMode
                        << " cbpLuma=" << cbpLuma
                        << " cbpChroma=" << cbpChroma
                        << " range=" << engine.codIRange()
                        << " offset=" << engine.codIOffset());
            }

            if (mbType == 0U) {
                decodeI4x4Mb(engine, contexts.get(), frame, mbInfoArray.get(),
                             widthInMbs, mbX, mbY, currentQp,
                             pps->chromaQpIndexOffset_);
            } else if (mbType >= 1U && mbType <= 24U) {
                uint32_t val = mbType - 1U;
                uint32_t i16x16PredMode = val % 4U;
                val /= 4U;
                uint32_t cbpChroma = val % 3U;
                uint32_t cbpLumaFlag = val / 3U;
                uint32_t cbpLuma = cbpLumaFlag ? 0x0FU : 0U;
                decodeI16x16Mb(engine, contexts.get(), frame, mbInfoArray.get(),
                               widthInMbs, mbX, mbY, currentQp,
                               i16x16PredMode, cbpLuma, cbpChroma,
                               pps->chromaQpIndexOffset_);
            } else {
                MESSAGE("I_PCM or unknown mb_type at MB " << mbAddr);
                REQUIRE(false);
            }

            if (shouldTrace) {
                MESSAGE("  CBP=" << mbInfoArray[mbAddr].cbp
                        << " QP=" << mbInfoArray[mbAddr].qp
                        << " qpDelta=" << mbInfoArray[mbAddr].qpDelta
                        << " chromaMode=" << mbInfoArray[mbAddr].chromaPredMode
                        << " range=" << engine.codIRange()
                        << " offset=" << engine.codIOffset());

                // Show first few pixels
                uint32_t px = mbX * 16U;
                uint32_t py = mbY * 16U;
                MESSAGE("  pixel(0,0)=" << (int)frame.y(px, py)
                        << " pixel(1,0)=" << (int)frame.y(px + 1, py)
                        << " pixel(0,1)=" << (int)frame.y(px, py + 1));
            }

            if (mbAddr < totalMbs - 1U) {
                uint32_t endFlag = engine.decodeTerminate();
                if (endFlag) {
                    MESSAGE("Early end of slice at MB " << mbAddr);
                    break;
                }
            }
        }

        // Show PSNR
        auto rawYuv = getFixture("bouncing_ball_ionly_cabac_raw.yuv");
        if (!rawYuv.empty()) {
            double yMse = 0.0;
            uint32_t w = frame.width(), h = frame.height();
            for (uint32_t y = 0; y < h; ++y)
                for (uint32_t x = 0; x < w; ++x) {
                    int32_t d = (int32_t)frame.y(x, y) - (int32_t)rawYuv[y * w + x];
                    yMse += d * d;
                }
            double psnr = 10.0 * std::log10(255.0 * 255.0 / (yMse / (w * h)));
            MESSAGE("Y PSNR: " << psnr << " dB");
        }

        break; // Only process first IDR
    }
}
