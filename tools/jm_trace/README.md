# JM Reference Decoder — CABAC Bin Trace Patch

This directory contains a patch that instruments the JM reference H.264 decoder
(`JM 19.0`) to emit per-bin CABAC trace output compatible with the Sub0h264
lock-step comparison tool.

## What the patch does

Adds per-bin trace emission to `source/app/ldecod/biaridecod.c`:

- `JM_SLICE_START slice=N R=... O=...` — one line per CABAC init (slice boundary)
- `JM_BIN N: s=... mps=... R=... V=... bl=... rLPS=... -> bit=... R2=... V2=... bl2=...`
  — one line per decoded bin (regular and bypass/eq_prob)

Key design decisions:
- `JM_TRACE_FROM_SLICE 1` — skips the IDR slice (slice 0), traces from the first
  P-frame onwards. This aligns with Sub0h264's default `--slice 1` argument.
- `JM_TRACE_MAX_BINS INT_MAX` — no artificial cap; captures the entire slice.
- Both the fast-path (MPS, range >= QUARTER) and the renorm path emit trace, with
  slice-count guards on both so IDR bins never appear in the P-frame trace.

## Applying the patch

The JM source is in `docs/reference/jm/` (gitignored — you need a copy of JM 19.0
checked out there first):

```bash
cd docs/reference/jm
git apply ../../tools/jm_trace/jm_trace.patch
```

## Building JM (Linux/WSL)

```bash
cd docs/reference/jm
mkdir build && cd build
cmake ..
make ldecod
```

The resulting binary is `build/ldecod/ldecod`.

## Running JM to produce a trace

Decode a fixture and capture stderr:

```bash
docs/reference/jm/build/ldecod/ldecod -i tests/fixtures/bouncing_ball_main.h264 \
    -o /dev/null 2> jm_trace.txt
```

## Running Sub0h264 to produce a trace

Build the trace tool (links against the traced library variant):

```bash
cmake --preset default
cmake --build --preset default --target sub0h264_trace
```

Decode the same fixture:

```bash
build/default/test_apps/trace/sub0h264_trace \
    tests/fixtures/bouncing_ball_main.h264 --level entropy --slice 1 2> our_trace.txt
```

## Comparing the two traces

```bash
python scripts/lockstep_compare.py our_trace.txt jm_trace.txt
```

Both decoders should emit identical bin sequences for every slice. The first
divergence pinpoints the exact decode operation where the two implementations
disagree.

## Trace format reference

### Sub0h264 (`our_trace.txt`)

```
OUR_SLICE_START slice=1 R=510 O=<offset>
<binIdx> <pre_state_raw> <post_mpsState> <decoded_bit> <post_range> <post_offset> <ctxIdx>
<binIdx> BP <decoded_bit> <post_range> <post_offset>
```

### JM (`jm_trace.txt`)

```
JM_SLICE_START slice=1 R=254 O=<offset> Dvalue=<raw>
JM_BIN N: s=<state> mps=<mps> R=<range> V=<value> bl=<bitsLeft> rLPS=<rLPS> -> bit=<bit> R2=<range2> V2=<value2> bl2=<bitsLeft2>
```

Note: JM uses `Drange` in the 9-bit [2,510] space (half the Sub0h264 range of
[4,1020]). The comparison script normalises both to the same scale.
