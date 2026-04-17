#!/usr/bin/env bash
# Generate per-bin CABAC trace from JM ldecod (built with jm_trace patch).
#
# Usage: scripts/gen_jm_bin_trace.sh <input.h264> <out_bin_trace.txt> [out_yuv]
#
# JM emits JM_BIN/JM_SLICE_START lines on stderr. We redirect stderr to the
# bin trace file; the YUV output is dumped to /tmp by default (caller may
# override with the optional 3rd argument).

set -euo pipefail

INPUT="${1:?usage: gen_jm_bin_trace.sh <input.h264> <out_bin.txt> [out_yuv]}"
OUT="${2:?usage: gen_jm_bin_trace.sh <input.h264> <out_bin.txt> [out_yuv]}"
YUV="${3:-/tmp/jm_decoded.yuv}"
REF="/tmp/jm_ref_unused.yuv"

JM="docs/reference/jm/bin/vs18/msvc-19.50/x86_64/release/ldecod.exe"

"$JM" -p InputFile="$INPUT" -p OutputFile="$YUV" -p RefFile="$REF" \
      -p Silent=1 2> "$OUT" > /dev/null

echo "JM bin trace -> $OUT  (YUV -> $YUV)"
