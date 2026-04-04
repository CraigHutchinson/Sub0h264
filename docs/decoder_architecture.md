# Decoder Architecture — Hierarchical Process Flow

Data flow from Annex B byte stream to reconstructed frame, with
per-stage status and optimisation opportunities.

## Level 0: Stream → Frames

```
Annex B stream → [NAL Parser] → [Slice Decoder] → [Deblock] → Reconstructed Frame
                       ↓              ↓                ↓
                    SPS/PPS      Per-MB loop        Per-MB edges
                    storage      (Level 1)          (Level 2)
```

**Data copies identified:**
- I-slice: currentFrame_ → DPB (460 KB memcpy per frame) — **TODO: eliminate**
- P-slice: eliminated (currentFrame() returns DPB pointer directly)
- Intra-in-P: neighbourhood copy ~96 bytes per intra MB (was 460 KB)

## Level 1: Slice → Macroblocks

```
Slice Header → [CABAC/CAVLC Init] → for each MB:
  ↓
  [mb_type decode] → [Prediction Mode] → [Residual Decode] → [Reconstruct]
        ↓                   ↓                   ↓                  ↓
   I_4x4/I_16x16      Intra modes          Entropy              IDCT +
   P_Skip/P_16x16     or MV+MC             (§9.2/§9.3)         dequant +
   P_8x8/P_8x8ref0                                              pred add
```

**Per-MB breakdown (profiled on desktop, 640x480 Baseline CAVLC):**
- Entropy decode: ~40% (CAVLC table lookups, bit reading)
- Prediction: ~25% (intra direction calc or MC interpolation)
- Transform: ~15% (IDCT butterfly + dequant)
- Overhead: ~20% (context lookup, NNZ tracking, MB setup)

## Level 2: Macroblock → Blocks

### I_4x4 MB (16 × 4x4 luma blocks + 4 × chroma)

```
for blk in scan_order(16):
  [Get Neighbours] → [Predict 4x4] → [CAVLC/CABAC] → [Zigzag] → [Dequant] → [IDCT] → [Add Pred] → [Store]
       ↓                  ↓                ↓              ↓          ↓           ↓          ↓
  Left/top NNZ     9 modes §8.3.1    coeff_token    scan→raster  QP-based    butterfly   clip[0,255]
  Left/top pixels  DC fallback       levels/zeros   cZigzag4x4   per-pos     >>6 norm    to frame
```

**Redundancy identified:**
- `getNeighborIntra4x4Mode()` recalculates MB offsets per block — cache MB base pointer
- `getLumaNc()` multiplies mbIdx*16 per block — precompute row pointer
- Zigzag reorder (16-entry permutation) done per block — could fuse with IDCT

### P_16x16 MB (1 luma MC + 2 chroma MC + residual)

```
[MVD decode] → [MV Predict] → [Luma MC 16x16] → [Chroma MC 8x8 ×2] → [Residual 16×4x4] → [Add to MC]
     ↓              ↓               ↓                   ↓                     ↓
  CAVLC/CABAC   median3()      6-tap filter         bilinear             per-block IDCT
  se(v)/bypass   §8.4.1.3      getSample()          w00..w11             same as I_4x4
```

**Redundancy identified:**
- `getSample()` per-pixel boundary check — fast path for in-bounds blocks (done)
- Luma MC allocates 256-byte temp `predLuma[]` on stack per MB — reuse frame-level buffer
- Chroma MC duplicates luma MC structure — could share filter kernel

### Deblocking (post-MB-decode)

```
for each MB in raster order:
  [Compute BS vertical] → [Filter vertical edges] → [Compute BS horizontal] → [Filter horizontal edges]
       ↓                         ↓                         ↓                          ↓
  4 edges × 4 pels        alpha/beta/tc0             4 edges × 4 pels           same filter
  isIntra/NNZ/MV/ref       threshold check           (after vertical)           kernel
```

**Redundancy identified:**
- BS computed per edge pair — precompute BS array for entire MB (4×4 = 16 values)
- `deblockMb()` calls `frame.yRow()` per pixel — inline row pointer for whole edge
- Same filter kernel called for both vertical/horizontal with different stride — template

## Level 3: Entropy Decode Detail

### CAVLC Path (Baseline)

```
[nC lookup] → [coeff_token VLC] → [trailing ones signs] → [level decode] → [total_zeros] → [run_before]
     ↓              ↓                    ↓                      ↓                ↓               ↓
  left+top NNZ   Table 9-5          bypass bits           prefix+suffix     Table 9-7/9-9    Table 9-10
  average        4 sub-tables        ≤3 bits              levelSuffixSize   one per tc       one per coeff
```

**Bottleneck:** VLC table lookup (sequential bit matching). Pre-sorted tables help.

### CABAC Path (High)

```
[Context Init] → for each bin:
  [State Lookup] → [rangeLPS] → [Compare offset] → [MPS/LPS select] → [State Transition] → [Renormalize]
       ↓               ↓              ↓                   ↓                   ↓                  ↓
  ctx[idx].mps    cCabacTable    codIOffset≥range?    symbol=mps^isLPS    next state          CLZ+shift
  7-bit packed    [128][4]       branch or CMOV       table bits 8-14     table bits 15-21    batch read
```

**Bottleneck:** `decodeBin()` called per binary decision (~500 times per MB).
Optimised: fast readBit(), batched renormalize via CLZ.
Remaining: table access pattern (128×4 = 512 entries, fits L1 on ESP32-P4).

## Data Flow Summary

```
           Annex B (bytes)
                ↓
         NAL extraction (no copy — pointer into input)
                ↓
         RBSP (emulation removed — one alloc per NAL)
                ↓
         Slice header parse (bit reader, no alloc)
                ↓
    ┌─── I-slice: write to currentFrame_ ──→ copy to DPB (TODO: eliminate)
    │
    ├─── P-slice: write directly to DPB frame (zero-copy for inter MBs)
    │       └── intra-in-P: copy neighbourhood (~96 bytes) to currentFrame_
    │
    └─── B-slice: NOT IMPLEMENTED
                ↓
         Deblock filter (in-place on decoded frame)
                ↓
         currentFrame() → returns DPB frame pointer (zero-copy API)
```

**Remaining copy to eliminate:** I-slice currentFrame_ → DPB (Phase 5).
Could be done by decoding I-slice directly into DPB-allocated frame.
