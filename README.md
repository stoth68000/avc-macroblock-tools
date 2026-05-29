# AVC Macroblock Tools

Production-oriented AVC/H.264 syntax inspection scaffolding for transport-stream probes and analyzers.

This first milestone provides:

- Annex B NAL unit extraction
- NAL header classification
- EBSP-to-RBSP conversion
- Bitreader with `u(n)`, `ue(v)`, `se(v)`, and RBSP trailing-bit checks
- Partial SPS/PPS parsing for stream structure metadata
- Partial slice-header parsing using active SPS/PPS
- Slice-header walking for reference-list modification, prediction weights, and decoded-reference-picture marking
- Staged `slice_data()` parsing with macroblock callbacks
- CAVLC I/P macroblock prediction, CBP, QP-delta, I_PCM, and residual walking
- CAVLC residual-block inspection scaffold
- CABAC arithmetic decoder with range-LPS and state-transition tables
- Neighbor-aware CABAC macroblock skip context tracking
- CABAC helpers for CBP, transform-size, QP-delta, and residual coefficient bins
- CABAC intra macroblock prediction parsing for I-slice macroblocks
- CABAC luma residual category planning for 4x4, 8x8, and Intra16x16 blocks
- CABAC I_PCM payload consumption and chroma residual walking for I macroblocks
- CABAC P-slice L0 prediction parsing for 16x16, 16x8, 8x16, and 8x8 macroblocks
- Partition-aware P-slice ref_idx and MVD context inputs from current/left/top prediction state
- CABAC B-slice inter prediction syntax for L0, L1, bi-pred, direct, and B_8x8 macroblocks
- Explicit direct-partition reporting for B direct and B-skip syntax
- DPB-backed reference-list inspection and derived inter motion-vector fields for macroblock comparison
- Callback-based parser API
- A small CLI that emits newline-delimited JSON records

It intentionally does not decode pixels. Macroblock-layer parsing, CAVLC residual parsing, and CABAC are now split into separate modules so deeper syntax work can proceed without turning the probe into an accidental monolith.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

## Run

```sh
./build/avc-probe sample.h264
./build/avc-probe --mb 120 sample.h264
```

Input is expected to be Annex B AVC elementary stream data. If you are probing MPEG-TS, demux PES payloads before passing the AVC byte stream to this library.

The optional `--mb` filter keeps slice/NAL context but limits macroblock, prediction, and residual JSON events to a single macroblock address. Residual events include coefficient-level entries so they can be compared against inspection tools.

## Layout

- `include/avc/avc_bitreader.h`: RBSP bit access primitives
- `include/avc/avc_nal.h`: Annex B NAL extraction and NAL metadata
- `include/avc/avc_syntax.h`: public SPS/PPS/slice syntax structs
- `include/avc/avc_macroblock.h`: slice-data, macroblock, and residual callback structs
- `include/avc/avc_cavlc.h`: CAVLC residual block inspection API
- `include/avc/avc_cabac.h`: CABAC arithmetic/context API
- `include/avc/avc_parser.h`: callback API and parser state
- `src/`: implementation
- `tools/avc_probe.c`: CLI JSON metadata emitter
- `tests/test_bitreader.c`: first focused unit test

## Staged TODOs

- Complete SPS syntax: scaling matrices, high-profile edge cases, complete VUI/HRD
- Complete PPS syntax: FMO/slice groups and scaling lists
- Complete slice header syntax: advanced decoded-reference-picture-state reconstruction
- Access-unit detection from VCL identity deltas
- Recovery point and timing SEI parsing
- Complete macroblock-layer syntax: top-right motion-vector predictor refinement and direct-mode MV derivation
- Complete CAVLC residual integration: scan-table selection for all frame/field and transform cases
- Complete CABAC: full standard context initialization table coverage and remaining exact ctxIdx derivation
- StreamEye-style visual comparison: inverse quantization/transform, prediction pixels, and reconstructed macroblock samples

## Current Macroblock/Entropy Boundary

The parser now records the bit offset at the end of each parsed slice header and enters `slice_data()`.

For CAVLC slices, I, P/SP, and B macroblocks now walk prediction syntax before CBP/residual consumption, including intra prediction modes, P L0 prediction syntax, B direct/L0/L1/bi-pred/B_8x8 syntax, coded-block-pattern mapping, optional transform 8x8, QP delta, I_PCM sample payloads, luma residual categories, and chroma DC/AC residual categories. B-slice `mb_skip_run` entries are reported as direct macroblocks with `direct_flag` and `direct_spatial_mv_pred_flag`.

For CABAC slices, the code byte-aligns after the slice header, initializes the CABAC arithmetic decoder, walks macroblocks until the terminate bin or picture bounds, and reports either skip state or macroblock type. The arithmetic engine now has the range-LPS table, MPS/LPS transitions, bypass bins, terminate bins, and table-driven context initialization for the syntax helpers currently consumed by the macroblock walker. The slice walker also keeps a per-macroblock neighbor-state map so `mb_skip_flag` uses left/top availability and skip state.

CABAC I-slice macroblocks now walk intra `mb_pred`, including Intra4x4/Intra8x8 prediction-mode flags and `intra_chroma_pred_mode`, before consuming CBP and QP-delta bins. Residual inspection is category-driven for luma 4x4, luma 8x8, Intra16x16 DC/AC, chroma DC, and chroma AC blocks. I_PCM macroblocks are byte-aligned and consumed using SPS bit-depth and chroma-format metadata.

P-slice inter macroblocks now walk L0 prediction syntax before CBP/residual consumption, including P_16x16, P_16x8, P_8x16, P_8x8, P_8x8ref0, sub-macroblock type, optional `ref_idx_l0`, and L0 motion-vector differences. Ref-index and MVD CABAC contexts use current-macroblock partition geometry plus stored left/top prediction state.

B-slice inter macroblocks now walk prediction syntax before CBP/residual consumption for L0, L1, bi-pred, direct, and B_8x8 forms. Direct partitions and B-skip are reported explicitly with `direct_flag` and `direct_spatial_mv_pred_flag`; they consume no explicit ref_idx/MVD syntax, matching the bitstream structure.

Unsupported decoded macroblock types are treated as parse failures instead of being skipped, so callers do not accidentally trust metadata from a desynchronized slice.
