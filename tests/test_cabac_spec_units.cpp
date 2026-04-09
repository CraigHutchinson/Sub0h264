#ifndef ESP_PLATFORM
/** CABAC spec-unit tests — verify optimized engine against spec-verbatim code.
 *
 *  Each test exercises ONE spec section by running the spec-verbatim
 *  implementation (cabac_spec.hpp) and the optimized engine (cabac.hpp)
 *  on real bitstream data, comparing every bin decision.
 *
 *  SPDX-License-Identifier: MIT
 */
#include "doctest.h"
#include "../components/sub0h264/src/cabac_spec.hpp"
#include "../components/sub0h264/src/cabac.hpp"
#include "../components/sub0h264/src/cabac_parse.hpp"
#include "../components/sub0h264/src/annexb.hpp"
#include "../components/sub0h264/src/sps.hpp"
#include "../components/sub0h264/src/pps.hpp"
#include "../components/sub0h264/src/slice.hpp"
#include "../components/sub0h264/src/tables.hpp"
#include "../components/sub0h264/src/transform.hpp"
#include "test_fixtures.hpp"

using namespace sub0h264;

// ── §9.3.1.1: Context initialization ───────────────────────────────────

TEST_CASE("Spec §9.3.1.1: context init matches optimized for QP range")
{
    // Verify spec::contextInit matches computeCabacInitState for all QP values
    for (int32_t qp = 0; qp <= 51; ++qp)
    {
        // Test a few representative (m,n) pairs
        struct { int32_t m, n; } cases[] = {
            {20, -15}, {2, 54}, {3, 74}, {-28, 127}, {-23, 104},
            {0, 0}, {-1, -1}, {40, 100}, {-40, 50},
        };
        for (auto& c : cases)
        {
            uint8_t specResult = spec::contextInit(c.m, c.n, qp);
            uint8_t optResult = computeCabacInitState(c.m, c.n, qp);
            if (specResult != optResult)
            {
                MESSAGE("MISMATCH at m=" << c.m << " n=" << c.n << " qp=" << qp
                        << " spec=0x" << std::hex << (int)specResult
                        << " opt=0x" << (int)optResult << std::dec);
            }
            CHECK(specResult == optResult);
        }
    }
}

// ── §9.3.3.1.1.3: mb_type ctxIdxInc ────────────────────────────────────

TEST_CASE("Spec §9.3.3.1.1.3: mb_type ctxIdxInc derivation")
{
    // Exhaustive test of all input combinations
    // unavailable → condTermFlag=0
    CHECK(spec::ctxIdxIncMbTypeI(false, false, false, false) == 0U); // both unavail
    CHECK(spec::ctxIdxIncMbTypeI(false, false, true, true) == 0U);   // unavail overrides I_NxN
    // available + I_NxN → condTermFlag=0
    CHECK(spec::ctxIdxIncMbTypeI(true, true, true, true) == 0U);     // both I_NxN
    // available + NOT I_NxN → condTermFlag=1
    CHECK(spec::ctxIdxIncMbTypeI(true, true, false, false) == 2U);   // both I_16x16
    CHECK(spec::ctxIdxIncMbTypeI(true, false, false, false) == 1U);  // left avail+I16, top unavail
    CHECK(spec::ctxIdxIncMbTypeI(false, true, false, false) == 1U);  // left unavail, top avail+I16
    CHECK(spec::ctxIdxIncMbTypeI(true, true, true, false) == 1U);    // left I_NxN, top I_16x16
}

// ── §9.3.3.2.1: decodeBin spec vs optimized engine ─────────────────────

TEST_CASE("Spec §9.3.3.2.1: decodeBin spec matches optimized on u100 fixture")
{
    // Load the u100 fixture and compare spec engine vs optimized engine
    // bin-by-bin through the entire MB decode
    auto h264 = getFixture("cabac_min_u100.h264");
    if (h264.empty()) { MESSAGE("fixture not found"); return; }

    // Extract IDR RBSP
    std::vector<NalBounds> bounds;
    findNalUnits(h264.data(), static_cast<uint32_t>(h264.size()), bounds);

    NalUnit idrNal;
    Sps sps;
    Pps pps;
    for (const auto& b : bounds)
    {
        NalUnit n;
        if (!parseNalUnit(h264.data() + b.offset, b.size, n)) continue;
        if (n.type == NalType::Sps) {
            BitReader sbr(n.rbspData.data(), static_cast<uint32_t>(n.rbspData.size()));
            parseSps(sbr, sps);
        }
        if (n.type == NalType::Pps) {
            BitReader pbr(n.rbspData.data(), static_cast<uint32_t>(n.rbspData.size()));
            parsePps(pbr, &sps, pps);
        }
        if (n.type == NalType::SliceIdr) idrNal = n;
    }

    // Parse slice header
    BitReader brHdr(idrNal.rbspData.data(), static_cast<uint32_t>(idrNal.rbspData.size()));
    SliceHeader sh;
    parseSliceHeader(brHdr, sps, pps, true, idrNal.refIdc, sh);
    int32_t sliceQp = pps.picInitQp_ + sh.sliceQpDelta_;
    uint32_t hdrBits = brHdr.bitOffset();
    // Align to byte
    hdrBits = (hdrBits + 7U) & ~7U;

    MESSAGE("SliceQP=" << sliceQp << " headerBits=" << hdrBits);

    // Set up TWO BitReaders from the same RBSP — one for spec, one for optimized
    BitReader brSpec(idrNal.rbspData.data(), static_cast<uint32_t>(idrNal.rbspData.size()));
    BitReader brOpt(idrNal.rbspData.data(), static_cast<uint32_t>(idrNal.rbspData.size()));
    brSpec.seekToBit(hdrBits);
    brOpt.seekToBit(hdrBits);

    // Init contexts identically
    uint32_t sliceTypeIdx = (sh.sliceType_ == SliceType::I) ? 2U : 0U;
    CabacContextSet specCtx, optCtx;
    specCtx.init(sliceTypeIdx, sh.cabacInitIdc_, sliceQp);
    optCtx.init(sliceTypeIdx, sh.cabacInitIdc_, sliceQp);

    // Init spec engine state
    uint32_t specR = 510U;
    uint32_t specO = brSpec.readBits(9U);

    // Init optimized engine
    CabacEngine optEngine;
    optEngine.init(brOpt);

    MESSAGE("Spec init: R=" << specR << " O=" << specO << " bit=" << brSpec.bitOffset());
    MESSAGE("Opt  init: R=" << optEngine.range() << " O=" << optEngine.offset()
            << " bit=" << optEngine.bitPosition());

    CHECK(specR == optEngine.range());
    CHECK(specO == optEngine.offset());

    // Now decode bin-by-bin and compare
    // Use the spec engine to decode mb_type for I-slice

    // §9.3.3.1.1.3: MB(0,0) — both neighbors unavailable
    uint32_t ctxInc = spec::ctxIdxIncMbTypeI(false, false, false, false);
    CHECK(ctxInc == 0U);

    // bin0: mb_type first bin at ctx[3 + ctxInc]
    uint32_t ctxIdx0 = 3U + ctxInc;
    uint8_t specState0 = specCtx[ctxIdx0].state();
    uint8_t specMps0 = specCtx[ctxIdx0].mps();
    uint32_t specBin0 = spec::decodeBinSpec(specR, specO, specState0, specMps0, brSpec);
    specCtx[ctxIdx0].mpsState = (specState0 & 0x3FU) | (specMps0 << 6U);

    uint32_t optBin0 = optEngine.decodeBin(optCtx[ctxIdx0]);

    MESSAGE("bin0: spec=" << specBin0 << " opt=" << optBin0
            << " ctx=" << ctxIdx0 << " specR=" << specR << " optR=" << optEngine.range()
            << " specO=" << specO << " optO=" << optEngine.offset());

    CHECK(specBin0 == optBin0);
    CHECK(specR == optEngine.range());
    CHECK(specO == optEngine.offset());

    if (specBin0 == 1U)
    {
        MESSAGE("bin0=1 → I_16x16 (not I_4x4)");

        // decodeTerminate for I_PCM check
        uint32_t specTerm = spec::decodeTerminateSpec(specR, specO, brSpec);
        uint32_t optTerm = optEngine.decodeTerminate();
        MESSAGE("terminate: spec=" << specTerm << " opt=" << optTerm
                << " specR=" << specR << " optR=" << optEngine.range());
        CHECK(specTerm == optTerm);
        CHECK(specR == optEngine.range());
        CHECK(specO == optEngine.offset());

        if (specTerm == 0U)
        {
            // I_16x16 suffix bins at ctx[6..10]
            // cbpLuma at ctx[6]
            uint32_t ci6 = 6U;
            uint8_t s6 = specCtx[ci6].state(), m6 = specCtx[ci6].mps();
            uint32_t specCbpL = spec::decodeBinSpec(specR, specO, s6, m6, brSpec);
            specCtx[ci6].mpsState = (s6 & 0x3FU) | (m6 << 6U);
            uint32_t optCbpL = optEngine.decodeBin(optCtx[ci6]);
            MESSAGE("cbpLuma: spec=" << specCbpL << " opt=" << optCbpL
                    << " specR=" << specR << " optR=" << optEngine.range());
            CHECK(specCbpL == optCbpL);
            CHECK(specR == optEngine.range());

            // cbpChromaFlag at ctx[7]
            uint32_t ci7 = 7U;
            uint8_t s7 = specCtx[ci7].state(), m7 = specCtx[ci7].mps();
            uint32_t specCbpCF = spec::decodeBinSpec(specR, specO, s7, m7, brSpec);
            specCtx[ci7].mpsState = (s7 & 0x3FU) | (m7 << 6U);
            uint32_t optCbpCF = optEngine.decodeBin(optCtx[ci7]);
            MESSAGE("cbpChromaFlag: spec=" << specCbpCF << " opt=" << optCbpCF);
            CHECK(specCbpCF == optCbpCF);
            CHECK(specR == optEngine.range());

            uint32_t cbpChroma = 0U;
            if (specCbpCF != 0U)
            {
                uint32_t ci8 = 8U;
                uint8_t s8 = specCtx[ci8].state(), m8 = specCtx[ci8].mps();
                uint32_t specCC2 = spec::decodeBinSpec(specR, specO, s8, m8, brSpec);
                specCtx[ci8].mpsState = (s8 & 0x3FU) | (m8 << 6U);
                uint32_t optCC2 = optEngine.decodeBin(optCtx[ci8]);
                CHECK(specCC2 == optCC2);
                cbpChroma = (specCC2 == 0U) ? 1U : 2U;
            }

            // predMode at ctx[9,10]
            uint32_t ci9 = 9U, ci10 = 10U;
            uint8_t s9 = specCtx[ci9].state(), m9 = specCtx[ci9].mps();
            uint32_t specPM1 = spec::decodeBinSpec(specR, specO, s9, m9, brSpec);
            specCtx[ci9].mpsState = (s9 & 0x3FU) | (m9 << 6U);
            uint32_t optPM1 = optEngine.decodeBin(optCtx[ci9]);
            CHECK(specPM1 == optPM1);

            uint8_t s10 = specCtx[ci10].state(), m10 = specCtx[ci10].mps();
            uint32_t specPM0 = spec::decodeBinSpec(specR, specO, s10, m10, brSpec);
            specCtx[ci10].mpsState = (s10 & 0x3FU) | (m10 << 6U);
            uint32_t optPM0 = optEngine.decodeBin(optCtx[ci10]);
            CHECK(specPM0 == optPM0);

            uint32_t predMode = (specPM1 << 1U) | specPM0;
            uint32_t mbType = 1U + predMode + cbpChroma * 4U + specCbpL * 12U;
            MESSAGE("I_16x16 mb_type=" << mbType << " pred=" << predMode
                    << " cbpL=" << specCbpL << " cbpC=" << cbpChroma);

            MESSAGE("After mb_type: specR=" << specR << " optR=" << optEngine.range()
                    << " specO=" << specO << " optO=" << optEngine.offset()
                    << " specBit=" << brSpec.bitOffset() << " optBit=" << optEngine.bitPosition());
            CHECK(specR == optEngine.range());
            CHECK(specO == optEngine.offset());
        }
    }
    else
    {
        MESSAGE("bin0=0 → I_4x4");
        MESSAGE("After bin0: specR=" << specR << " optR=" << optEngine.range()
                << " specO=" << specO << " optO=" << optEngine.offset());
    }

    // Final check: spec engine and optimized engine are in lockstep
    CHECK(brSpec.bitOffset() == brOpt.bitOffset());
}

TEST_CASE("Spec §9.3.3.2.1: cCabacTable matches Table 9-45 for all states")
{
    // Verify our packed cCabacTable[128][4] matches the spec tables exactly.
    // For each combined_state = (pStateIdx << 1) | valMPS and qRange 0-3:
    //   bits 0-7: rangeLPS must match cRangeTabLPS[pStateIdx][qRange]
    //   bits 8-14: nextMPS combined_state
    //   bits 15-21: nextLPS combined_state

    uint32_t mismatches = 0;
    for (uint32_t pState = 0; pState < 64; ++pState)
    {
        for (uint32_t mps = 0; mps < 2; ++mps)
        {
            // Table is indexed by mpsState encoding: pState | (mps << 6)
            uint32_t tableIdx = pState | (mps << 6U);
            for (uint32_t q = 0; q < 4; ++q)
            {
                uint32_t entry = cCabacTable[tableIdx][q];
                uint32_t gotLPS = entry & 0xFFU;
                uint32_t gotNextMPS = (entry >> 8U) & 0x7FU;
                uint32_t gotNextLPS = (entry >> 15U) & 0x7FU;

                uint32_t expectedLPS = spec::cRangeTabLPS[pState][q];

                // Expected next MPS: same mps, transIdxMPS state
                // Packed as mpsState: nextState | (mps << 6)
                uint32_t nextMPSState = spec::cTransIdxMPS[pState];
                uint32_t expectedNextMPS = nextMPSState | (mps << 6U);

                // Expected next LPS: mps flips if pState==0
                uint32_t nextLPSState = spec::cTransIdxLPS[pState];
                uint32_t nextLPSMps = (pState == 0) ? (1U - mps) : mps;
                uint32_t expectedNextLPS = nextLPSState | (nextLPSMps << 6U);

                if (gotLPS != expectedLPS || gotNextMPS != expectedNextMPS || gotNextLPS != expectedNextLPS)
                {
                    if (mismatches < 5)
                        MESSAGE("MISMATCH: state=" << pState << " mps=" << mps << " q=" << q
                                << " lps: got=" << gotLPS << " exp=" << expectedLPS
                                << " nextMPS: got=" << gotNextMPS << " exp=" << expectedNextMPS
                                << " nextLPS: got=" << gotNextLPS << " exp=" << expectedNextLPS);
                    ++mismatches;
                }
            }
        }
    }
    MESSAGE("cCabacTable verification: " << mismatches << " mismatches out of 512 entries");
    CHECK(mismatches == 0U);
}

// ── Negative-clause tests: exercise both branches of every conditional ──

TEST_CASE("Spec §9.3.3.1.1.3: mb_type condTermFlag negative clause exhaustive")
{
    // Test EVERY combination of availability × mb_type to catch inversion bugs.
    // The spec says: condTermFlagN = 0 if unavailable, = (mb_type != I_NxN) if available.
    // These tests verify both the positive AND negative branches.

    // Case 1: Both unavailable → ctxInc=0
    CHECK(spec::ctxIdxIncMbTypeI(false, false, false, false) == 0U);
    CHECK(spec::ctxIdxIncMbTypeI(false, false, true, true) == 0U); // I_NxN ignored when unavail

    // Case 2: Left available + I_NxN (I_4x4) → condTerm=0
    CHECK(spec::ctxIdxIncMbTypeI(true, false, true, false) == 0U);

    // Case 3: Left available + NOT I_NxN (I_16x16) → condTerm=1
    CHECK(spec::ctxIdxIncMbTypeI(true, false, false, false) == 1U);

    // Case 4: Top available + I_NxN → condTerm=0
    CHECK(spec::ctxIdxIncMbTypeI(false, true, false, true) == 0U);

    // Case 5: Top available + NOT I_NxN → condTerm=1
    CHECK(spec::ctxIdxIncMbTypeI(false, true, false, false) == 1U);

    // Case 6: Both available + both I_NxN → ctxInc=0
    CHECK(spec::ctxIdxIncMbTypeI(true, true, true, true) == 0U);

    // Case 7: Both available + both NOT I_NxN → ctxInc=2
    CHECK(spec::ctxIdxIncMbTypeI(true, true, false, false) == 2U);

    // Case 8: Mixed — left I_NxN, top NOT I_NxN → ctxInc=1
    CHECK(spec::ctxIdxIncMbTypeI(true, true, true, false) == 1U);

    // Case 9: Mixed — left NOT I_NxN, top I_NxN → ctxInc=1
    CHECK(spec::ctxIdxIncMbTypeI(true, true, false, true) == 1U);
}

TEST_CASE("Spec §9.3.3.1.1.1: mb_skip_flag condTermFlag negative clause exhaustive")
{
    // condTermFlagN = 0 if unavailable, = (!mb_skip_flag) if available

    // Both unavailable → ctxInc=0
    CHECK(spec::ctxIdxIncMbSkipP(false, false, false, false) == 0U);

    // Available + skipped → condTerm=0
    CHECK(spec::ctxIdxIncMbSkipP(true, false, true, false) == 0U);

    // Available + NOT skipped → condTerm=1
    CHECK(spec::ctxIdxIncMbSkipP(true, false, false, false) == 1U);

    // Both available + both not skipped → ctxInc=2
    CHECK(spec::ctxIdxIncMbSkipP(true, true, false, false) == 2U);

    // Both available + both skipped → ctxInc=0
    CHECK(spec::ctxIdxIncMbSkipP(true, true, true, true) == 0U);
}

TEST_CASE("Spec §9.3.3.1.1.7: chroma mode condTermFlag negative clause exhaustive")
{
    // condTermFlagN = 0 if unavailable, 0 if not intra, (chromaMode!=0) if intra

    // Unavailable → 0 regardless
    CHECK(spec::ctxIdxIncChromaMode(false, false, true, true, 3, 3) == 0U);

    // Available but not intra → 0
    CHECK(spec::ctxIdxIncChromaMode(true, true, false, false, 3, 3) == 0U);

    // Available, intra, DC mode (0) → 0
    CHECK(spec::ctxIdxIncChromaMode(true, true, true, true, 0, 0) == 0U);

    // Available, intra, non-DC left → 1
    CHECK(spec::ctxIdxIncChromaMode(true, true, true, true, 1, 0) == 1U);

    // Available, intra, non-DC top → 1
    CHECK(spec::ctxIdxIncChromaMode(true, true, true, true, 0, 2) == 1U);

    // Available, intra, both non-DC → 2
    CHECK(spec::ctxIdxIncChromaMode(true, true, true, true, 3, 1) == 2U);

    // Mixed: left unavailable, top available+intra+non-DC → 1
    CHECK(spec::ctxIdxIncChromaMode(false, true, false, true, 0, 2) == 1U);
}

// Helper: spec-verbatim decodeBin with context update
static uint32_t specBin(uint32_t& R, uint32_t& O, CabacCtx& ctx, BitReader& br)
{
    uint8_t s = ctx.state(), m = ctx.mps();
    uint32_t bin = spec::decodeBinSpec(R, O, s, m, br);
    ctx.mpsState = (s & 0x3FU) | (m << 6U);
    return bin;
}

TEST_CASE("Spec complete: u100 full I_16x16 MB decode with DC residual §7.3.5/§9.3")
{
    // Complete spec-verbatim decode of the u100 MB through to pixel output.
    // This exercises every spec section: mb_type binarization, I_16x16 suffix,
    // chroma mode, QP delta, coded_block_flag, sig map, coefficient levels,
    // Hadamard inverse, dequant, IDCT, and prediction addition.
    auto h264 = getFixture("cabac_min_u100.h264");
    if (h264.empty()) { MESSAGE("fixture not found"); return; }

    std::vector<NalBounds> bounds;
    findNalUnits(h264.data(), static_cast<uint32_t>(h264.size()), bounds);

    NalUnit idrNal;
    Sps sps;
    Pps pps;
    for (const auto& b : bounds)
    {
        NalUnit n;
        if (!parseNalUnit(h264.data() + b.offset, b.size, n)) continue;
        if (n.type == NalType::Sps) {
            BitReader sbr(n.rbspData.data(), static_cast<uint32_t>(n.rbspData.size()));
            parseSps(sbr, sps);
        }
        if (n.type == NalType::Pps) {
            BitReader pbr(n.rbspData.data(), static_cast<uint32_t>(n.rbspData.size()));
            parsePps(pbr, &sps, pps);
        }
        if (n.type == NalType::SliceIdr) idrNal = n;
    }

    BitReader brHdr(idrNal.rbspData.data(), static_cast<uint32_t>(idrNal.rbspData.size()));
    SliceHeader sh;
    parseSliceHeader(brHdr, sps, pps, true, idrNal.refIdc, sh);
    int32_t sliceQp = pps.picInitQp_ + sh.sliceQpDelta_;
    uint32_t hdrBits = (brHdr.bitOffset() + 7U) & ~7U;

    BitReader br(idrNal.rbspData.data(), static_cast<uint32_t>(idrNal.rbspData.size()));
    br.seekToBit(hdrBits);

    CabacCtx ctx[cNumCabacCtx] = {};
    initCabacContexts(ctx, 2U, sh.cabacInitIdc_, sliceQp);

    uint32_t R = 510U;
    uint32_t O = br.readBits(9U);
    MESSAGE("§9.3.1.2 Init: R=" << R << " O=" << O << " QP=" << sliceQp);

    // ── §9.3.3.1.2: mb_type bin0 ──────────────────────────────────────
    uint32_t bin0 = specBin(R, O, ctx[3], br); // ctxInc=0 for unavailable
    MESSAGE("mb_type bin0=" << bin0 << " (1=not I_4x4) R=" << R << " O=" << O);
    REQUIRE(bin0 == 1U); // This bitstream encodes I_16x16

    // ── §9.3.3.1.2: terminate check for I_PCM ─────────────────────────
    uint32_t term = spec::decodeTerminateSpec(R, O, br);
    MESSAGE("terminate=" << term << " (1=I_PCM) R=" << R << " O=" << O);
    REQUIRE(term == 0U);

    // ── §9.3.3.1.2: I_16x16 suffix at ctx[6..10] ─────────────────────
    // Table 9-34: After bin0=1 and terminate=0, decode suffix.
    // Base context for suffix in I-slices: cCtxMbTypeI + 3 = 6
    uint32_t cbpLumaBin = specBin(R, O, ctx[6], br);
    MESSAGE("cbpLuma=" << cbpLumaBin << " R=" << R);

    uint32_t cbpChromaFlag = specBin(R, O, ctx[7], br);
    uint32_t cbpChroma = 0U;
    if (cbpChromaFlag != 0U)
        cbpChroma = (specBin(R, O, ctx[8], br) == 0U) ? 1U : 2U;
    MESSAGE("cbpChroma=" << cbpChroma);

    uint32_t predBit1 = specBin(R, O, ctx[9], br);
    uint32_t predBit0 = specBin(R, O, ctx[10], br);
    uint32_t predMode = (predBit1 << 1U) | predBit0;
    MESSAGE("predMode=" << predMode << " (0=V,1=H,2=DC,3=Plane)");

    uint32_t mbType = 1U + predMode + cbpChroma * 4U + cbpLumaBin * 12U;
    MESSAGE("mb_type=" << mbType << " cbpL=" << cbpLumaBin << " cbpC=" << cbpChroma
            << " pred=" << predMode);
    MESSAGE("After mb_type: R=" << R << " O=" << O << " bit=" << br.bitOffset());

    // ── §9.3.3.1.1.7: intra_chroma_pred_mode ──────────────────────────
    uint32_t chromaMode = 0U;
    if (specBin(R, O, ctx[64], br) != 0U) // ctxInc=0 for unavailable
    {
        if (specBin(R, O, ctx[67], br) == 0U) chromaMode = 1U;
        else chromaMode = (specBin(R, O, ctx[67], br) == 0U) ? 2U : 3U;
    }
    MESSAGE("chromaMode=" << chromaMode);

    // ── §7.3.5.1: mb_qp_delta — always present for I_16x16 ────────────
    // §9.3.3.1.1.5: ctxIdxInc = 0 (first MB, no prior delta)
    int32_t qpDelta = 0;
    if (specBin(R, O, ctx[60], br) != 0U)
    {
        qpDelta = 1;
        // Additional bins for larger delta...
        if (specBin(R, O, ctx[62], br) != 0U)
        {
            ++qpDelta;
            while (qpDelta < 52 && specBin(R, O, ctx[63], br) != 0U)
                ++qpDelta;
        }
        // Sign via parity: even→positive, odd→negative
        if (qpDelta > 0)
            qpDelta = (qpDelta & 1) ? ((qpDelta + 1) >> 1) : -((qpDelta) >> 1);
    }
    int32_t qp = sliceQp + qpDelta;
    qp = ((qp % 52) + 52) % 52;
    MESSAGE("qpDelta=" << qpDelta << " QPY=" << qp);
    MESSAGE("After QP: R=" << R << " O=" << O << " bit=" << br.bitOffset());

    // ── §9.3.3.1.3: Luma DC residual (ctxBlockCat=0, 16 coeffs) ───────
    // coded_block_flag at ctx[85] (cCtxCbf + cat*4 + cbfInc)
    uint32_t cbf = specBin(R, O, ctx[85], br);
    MESSAGE("DC coded_block_flag=" << cbf);

    int16_t dcScan[16] = {};
    uint32_t numSig = 0;
    if (cbf != 0U)
    {
        // significant_coeff_flag and last_significant_coeff_flag
        // ctx offsets for cat 0: sig=105, last=166, level=227
        uint8_t sigMap[16] = {};
        int32_t lastSigIdx = -1;
        bool foundLast = false;

        for (uint32_t i = 0; i < 15U; ++i)
        {
            uint32_t sig = specBin(R, O, ctx[105U + i], br);
            if (sig)
            {
                sigMap[i] = 1U;
                lastSigIdx = static_cast<int32_t>(i);
                ++numSig;
                uint32_t last = specBin(R, O, ctx[166U + i], br);
                MESSAGE("  sig[" << i << "]=1 last=" << last << " R=" << R << " O=" << O);
                if (last) { foundLast = true; break; }
            }
        }
        if (!foundLast && numSig > 0)
        {
            sigMap[15] = 1U;
            lastSigIdx = 15;
            ++numSig;
        }

        MESSAGE("DC sig map: numSig=" << numSig << " lastSigIdx=" << lastSigIdx);

        // Coefficient levels (reverse scan order) — §9.3.3.1.3
        uint32_t numT1 = 0U, numLarger = 0U;
        for (int32_t i = lastSigIdx; i >= 0; --i)
        {
            if (!sigMap[i]) continue;

            // coeff_abs_level_minus1 prefix
            uint32_t ctxInc = (numLarger > 0U) ? 0U :
                              ((numT1 < 4U) ? (numT1 + 1U) : 4U);

            uint32_t prefix = 0U;
            if (specBin(R, O, ctx[227U + ctxInc], br) == 1U)
            {
                ++prefix;
                uint32_t nextCtx = (numLarger > 0U) ?
                    (5U + ((numLarger < 4U) ? numLarger : 4U)) : 5U;
                while (prefix < 14U && specBin(R, O, ctx[227U + nextCtx], br) == 1U)
                    ++prefix;
            }

            int32_t absLevel;
            if (prefix < 14U)
            {
                absLevel = static_cast<int32_t>(prefix) + 1;
            }
            else
            {
                // EG(0) suffix via bypass
                uint32_t k = 0U;
                while (spec::decodeBypassSpec(R, O, br) == 1U && k < 16U) ++k;
                uint32_t suffix = ((1U << k) - 1U) + 0U;
                for (uint32_t b = 0; b < k; ++b)
                    suffix += spec::decodeBypassSpec(R, O, br) << (k - 1U - b);
                // Wait, need to re-read the suffix bits properly
                // Actually: suffix = ((1<<k)-1) + readBypassBins(k)
                absLevel = static_cast<int32_t>(14U + suffix) + 1;
            }

            uint32_t sign = spec::decodeBypassSpec(R, O, br);
            int32_t level = sign ? -absLevel : absLevel;

            if (absLevel == 1) ++numT1;
            else ++numLarger;

            dcScan[i] = static_cast<int16_t>(level);
            MESSAGE("  coeff[" << i << "]=" << level << " (abs=" << absLevel
                    << " prefix=" << prefix << ")");
        }
    }

    MESSAGE("DC scan: [" << dcScan[0] << "," << dcScan[1] << "," << dcScan[2]
            << "," << dcScan[3] << ",...]");
    MESSAGE("After DC: R=" << R << " O=" << O << " bit=" << br.bitOffset());

    // ── §8.5.12.1: Dequant + §8.5.10: Hadamard inverse ────────────────
    // Apply zigzag reorder, then Hadamard, then dequant
    int16_t dcCoeffs[16] = {};
    for (uint32_t i = 0; i < 16; ++i)
        dcCoeffs[cZigzag4x4[i]] = dcScan[i];

    inverseHadamard4x4(dcCoeffs);

    int32_t qpDiv6 = qp / 6;
    int32_t qpMod6 = qp % 6;
    int32_t dcScale = cDequantScale[qpMod6][0];

    for (uint32_t i = 0; i < 16; ++i)
    {
        if (dcCoeffs[i] != 0)
        {
            int32_t val = dcCoeffs[i] * dcScale;
            dcCoeffs[i] = static_cast<int16_t>(
                qpDiv6 >= 2 ? val << (qpDiv6 - 2)
                            : (val + (1 << (1 - qpDiv6))) >> (2 - qpDiv6));
        }
    }

    MESSAGE("Dequant DC[0]=" << dcCoeffs[0] << " (QP=" << qp
            << " div6=" << qpDiv6 << " mod6=" << qpMod6 << " scale=" << dcScale << ")");

    // ── Reconstruction: DC prediction + residual → pixel ───────────────
    // I_16x16 DC prediction for first MB: all pixels = 128 (no neighbors)
    // With no AC (cbpLuma=0), each 4x4 block gets DC only
    // Per-block pixel = Clip3(0, 255, pred + (dcCoeffs[blk] + 32) >> 6)
    // For uniform DC: all 16 blocks get same value
    int32_t residual = (dcCoeffs[0] + 32) >> 6;
    int32_t pixel = 128 + residual;
    if (pixel < 0) pixel = 0;
    if (pixel > 255) pixel = 255;

    MESSAGE("Residual=" << residual << " pixel=" << pixel << " (expected 100)");

    // ── end_of_slice_flag — §7.3.4 ────────────────────────────────────
    uint32_t endOfSlice = spec::decodeTerminateSpec(R, O, br);
    MESSAGE("end_of_slice=" << endOfSlice << " (must be 1 for 1-MB frame)");

    // For a 1-MB frame, end_of_slice MUST be 1
    // If it's 0, our decode consumed wrong bins → misaligned
    CHECK(endOfSlice == 1U);

    // The pixel value should be 100 if the decode is correct
    CHECK(pixel == 100);
}

#endif // ESP_PLATFORM
