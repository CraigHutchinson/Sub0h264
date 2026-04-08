/** Spec-only H.264 reference decoder — Public API
 *
 *  Clean-room implementation based exclusively on:
 *    ITU-T H.264 (V16) 2021-06
 *    ISO/IEC 14496-10:2022
 *
 *  This decoder exists as an independent cross-reference to identify
 *  bugs in the main sub0h264 decoder. It shares only infrastructure
 *  (Frame buffer, SPS/PPS parsers, BitReader) with the main decoder.
 *  All decode logic (CABAC, dequant, IDCT, prediction) is implemented
 *  fresh from the specification.
 *
 *  SPDX-License-Identifier: MIT
 */
#ifndef SPEC_REF_HPP
#define SPEC_REF_HPP

#include <cstdint>

namespace sub0h264::spec_ref {

/// Spec edition this implementation targets.
inline constexpr const char* cSpecEdition = "ITU-T H.264 (V16) 2021-06";

} // namespace sub0h264::spec_ref

#endif // SPEC_REF_HPP
