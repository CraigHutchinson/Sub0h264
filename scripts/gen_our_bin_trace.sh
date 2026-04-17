#!/usr/bin/env bash
# Generate per-bin CABAC entropy trace from sub0h264_trace.
#
# Usage: scripts/gen_our_bin_trace.sh <input.h264> <out_bin_trace.txt> [slice=0]

set -euo pipefail

INPUT="${1:?usage: gen_our_bin_trace.sh <input.h264> <out_bin.txt> [slice]}"
OUT="${2:?usage: gen_our_bin_trace.sh <input.h264> <out_bin.txt> [slice]}"
SLICE="${3:-0}"

TRACE="build/test_apps/trace/Release/sub0h264_trace.exe"

"$TRACE" "$INPUT" --level entropy --slice "$SLICE" --dump /tmp/our_decoded.yuv > "$OUT" 2>&1

echo "Our bin trace -> $OUT"
