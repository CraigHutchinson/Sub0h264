#!/usr/bin/env python3
"""Audit decoder resilience — check what error handling exists and what's missing.

Scans the decoder source for bounds checks, error returns, and conformance tests.
Reports gaps that should be addressed per H.264 spec requirements.
"""
import os, re

SRC = "components/sub0h264/src"
files = [
    "annexb.hpp", "bitstream.hpp", "sps.hpp", "pps.hpp", "slice.hpp",
    "decoder.hpp", "cabac.hpp", "cabac_parse.hpp", "cavlc.hpp",
    "transform.hpp", "intra_pred.hpp", "inter_pred.hpp", "deblock.hpp",
    "dpb.hpp", "motion.hpp", "frame.hpp",
]

checks = {
    "bounds_check": re.compile(r"hasBits|isExhausted|sizeBytes_|< sizeBytes|>= sizeBytes|bitOffset_.*>=|< 0U|> 51|>= 48|< 48"),
    "error_return": re.compile(r"return.*Error|return false|return Result::Error|return DecodeStatus::Error"),
    "null_check": re.compile(r"nullptr|== nullptr|!= nullptr|== 0U.*return"),
    "clip_value": re.compile(r"clipU8|clampQp|Clip3|std::min|std::max"),
    "overflow_guard": re.compile(r"overflow|cExpGolombOverflow|> 31U|> 255"),
}

# Known resilience requirements from H.264 spec
requirements = [
    ("NAL forbidden_zero_bit check", "annexb.hpp", "forbiddenBit"),
    ("SPS parameter range validation", "sps.hpp", "ErrorInvalidParam"),
    ("PPS parameter range validation", "pps.hpp", "ErrorInvalidParam"),
    ("Slice header field validation", "slice.hpp", "ErrorInvalidParam"),
    ("CBP code range check (0-47)", "decoder.hpp", "cbpCode < 48"),
    ("QP range enforcement (0-51)", "decoder.hpp", "clampQp"),
    ("Exp-Golomb overflow detection", "bitstream.hpp", "cExpGolombOverflow"),
    ("CABAC engine range check", "cabac.hpp", "codIRange_"),
    ("CABAC end_of_slice_flag per MB", "decoder.hpp", "decodeTerminate"),
    ("Bitstream exhaustion check", "decoder.hpp", "isExhausted"),
    ("Reference frame null check", "decoder.hpp", "refFrame.*nullptr"),
    ("Frame allocation check", "frame.hpp", "isAllocated"),
    ("DPB slot availability", "dpb.hpp", "getDecodeTarget"),
    ("Motion vector range clipping", "inter_pred.hpp", "clip"),
    ("Pixel output clipping [0,255]", "transform.hpp", "clipU8"),
    ("Emulation prevention byte handling", "annexb.hpp", "emulationPrevention"),
    ("Start code detection robustness", "annexb.hpp", "findNalUnits"),
    ("Chroma format validation", "sps.hpp", "chromaFormatIdc"),
    ("MB address bounds check", "decoder.hpp", "mbAddr.*totalMbs"),
    ("NNZ array bounds", "decoder.hpp", "nnzLuma_"),
]

print("=== Decoder Resilience Audit ===\n")

found = []
missing = []

for desc, filename, pattern in requirements:
    filepath = os.path.join(SRC, filename)
    if not os.path.exists(filepath):
        missing.append((desc, filename, "FILE NOT FOUND"))
        continue

    content = open(filepath).read()
    if re.search(pattern, content):
        found.append((desc, filename))
    else:
        missing.append((desc, filename, "NOT FOUND"))

print("PRESENT (%d):" % len(found))
for desc, f in found:
    print("  [OK] %s (%s)" % (desc, f))

print("\nMISSING/GAPS (%d):" % len(missing))
for item in missing:
    print("  [!!] %s (%s) — %s" % item)

# Additional checks not in the source
print("\n=== Additional Resilience Requirements (from H.264 spec) ===")
additional = [
    "§7.3.4: end_of_slice_flag after every CABAC MB (JUST ADDED)",
    "§7.4.2.1: SPS seq_parameter_set_id in range [0, 31]",
    "§7.4.2.2: PPS pic_parameter_set_id in range [0, 255]",
    "§7.4.3: slice_type range validation",
    "§7.4.5: mb_qp_delta causing QP outside [0, 51]",
    "§8.4.2.2: Motion compensation reference out-of-frame clamping",
    "§9.1: Exp-Golomb leading zeros > 32 (malformed bitstream)",
    "§9.2: CAVLC table index out of range",
    "§9.3: CABAC codIRange falling to 0 (corrupted arithmetic)",
    "§A.3: Level-specific constraints (max MBs, max DPB size)",
    "§B.1: Annex B start code robustness (incomplete start codes)",
    "Annex C: Hypothetical Reference Decoder (HRD) conformance",
    "§6.4.1: MB address wrapping for slice boundaries",
    "Error concealment: replace corrupted MB with DC prediction",
    "Bitstream version/profile checking before decode attempt",
]
for item in additional:
    print("  [TODO] %s" % item)
