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
./build/avc-probe --picture 42 sample.h264
./build/avc-probe --picture 42 --slice 87 sample.h264
./build/avc-probe --picture 42 --slice-in-picture 0 --mb 120 sample.h264
./build/avc-probe --mb 120 sample.h264
```

Input is expected to be Annex B AVC elementary stream data. If you are probing MPEG-TS, demux PES payloads before passing the AVC byte stream to this library.

The probe emits `nal_index`, `picture_index`, `slice_index`, and `slice_in_picture` fields so long streams can be navigated picture-by-picture before drilling into a macroblock. The optional `--picture`, `--slice`, `--slice-in-picture`, and `--mb` filters keep NAL/picture context but limit slice, macroblock, prediction, and residual JSON events to the selected rows. Residual events include coefficient-level entries so they can be compared against inspection tools.

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

Goal: compare macroblock, prediction, residual, and coefficient details against StreamEye Studio. The parser now emits enough syntax to start comparing simple progressive 4:2:0 streams and to inspect high-profile residual metadata, but the items below still block cell-for-cell parity on general AVC content.

- Comparison-ready surfaces now present:
  - Probe output includes `nal_index`, `picture_index`, `slice_index`, and `slice_in_picture` fields, plus `--picture`, `--slice`, `--slice-in-picture`, and `--mb` filters for navigating long sequences before drilling into a macroblock.
  - SPS/PPS high-profile syntax, VUI/HRD, PPS FMO/scaling-list syntax, buffering-period/recovery-point/pic-timing SEI, and slice-header reference-list syntax are parsed and surfaced.
  - Macroblock events include entropy mode, skip state, `mb_field_decoding_flag`, `mb_type`, CBP, transform-8x8 flag, QP delta, derived `qp_y`, `qp_cb`, `qp_cr`, and I_PCM sample counts.
  - `mb_pred` events emit intra prediction modes, ref_idx, MVD, MVP, final MV, direct flags, list masks, sub-macroblock type slots, partition rectangles, sub-partition motion/reference rows, and resolved reference-picture identities when the active ref lists contain them.
  - CAVLC and CABAC residual events emit block identity, component/chroma layout, bit depth, per-block QP, transform-bypass state, scaling-list metadata, total coefficient count, coefficient level, scan index, block-local coordinates, and macroblock-local coordinates. CAVLC also emits trailing-one, total-zero, and run-before values.
  - Frame/field scan selection is centralized for CAVLC and CABAC residual coordinate placement, including 4x4, 8x8, AC-only, chroma DC, transform-bypass, and parsed/inferred MBAFF `mb_field_decoding_flag` scan modes.
  - Standard CABAC context initialization tables are wired for I/SI slices and P/B slices across every `cabac_init_idc`.
  - A first-pass DPB/reference-list model emits ref-list events and joins `ref_idx` output to frame-num/POC/long-term identity when available.

- First StreamEye parity targets:
  - Add a small corpus of StreamEye-exported fixtures: one progressive CAVLC I/P/B stream, one progressive CABAC I/P/B stream, one transform-8x8 stream, one Intra16x16 stream, and one chroma-residual-heavy stream.
  - Stabilize and version the NDJSON schema around StreamEye table columns: picture identity, macroblock address, macroblock type name, partition/sub-partition geometry, ref picture identity/POC, ref_idx, MVP, MVD, final MV, CBP, QP, residual block kind/index/component, scan index, coefficient level, coefficient x/y, transform-bypass, bit depth, and scaling-list identity.
  - Add a diff helper that compares this tool's NDJSON against StreamEye-exported values, with tolerances only where the standard permits equivalent representations.

- Access-unit, picture-state, and reference identity blockers:
  - Harden access-unit/picture boundary detection against the full VCL identity rules. Probe output now indexes pictures from parsed slice identity, but edge cases around field pairs, redundant pictures, POC wrapping, and MMCO still need validation.
  - Complete POC derivation for every POC type. Current reference identity is based on `pic_order_cnt_lsb + delta_pic_order_cnt_bottom`, which is not enough for type 1/2, wrapping, fields, or MMCO 5.
  - Expose current picture/frame/field identity directly on macroblock and residual events, not only through slice/ref-list context.
  - Complete decoded-reference-picture marking and DPB behavior: frame-num wrap/gaps, sliding-window ordering, MMCO 4/5/6 details, max-long-term handling, field pairs/complementary fields, and long-term refs.
  - Make FMO slice-group maps drive macroblock address progression; PPS syntax is parsed, but slice-data walking still assumes raster macroblock order.
  - Make field pictures and MBAFF affect neighbor availability and reference-picture identity everywhere. Residual scan selection uses field/MBAFF flags, but neighbor derivation is still raster-frame oriented.

- Macroblock prediction and motion-vector blockers:
  - Derive distinct MVD, MVP, and final MV per actual P_8x8/B_8x8 sub-partition. Output has per-sub-partition rows now, but rows currently inherit motion from the modeled parent partition where derivation is not yet split.
  - Store and emit exact ref_idx, MVD, MVP, and final MV per actual sub-partition for L0/L1 so StreamEye motion-vector tables can be compared cell-for-cell.
  - Use B-slice macroblock geometry in MVP/neighbor derivation for every B_Direct/B_L0/B_L1/B_Bi 16x16, 16x8, 8x16, and 8x8 form. Output geometry is emitted, but parts of motion derivation still use simplified neighbor shapes.
  - Finish direct-mode derivation by retaining colocated reference-picture macroblock motion maps in the DPB; current temporal-direct output cannot be exact without that state.
  - Refine top-right/top-left MVP candidate selection at sub-partition granularity, including unavailable-neighbor rules for FMO, field, and MBAFF pictures.

- CAVLC residual and coefficient blockers:
  - Complete `nC` derivation for FMO, MBAFF, field pictures, and all unavailable-neighbor cases. Intra-macroblock luma/chroma and 8x8-transform nonzero propagation are wired for raster progressive cases.
  - Verify `coeff_token`, `total_zeros`, `run_before`, trailing-one sign handling, AC-only scan offsets, chroma DC placement, and 4:2:2/4:4:4 block coordinates against standard vectors and StreamEye output.
  - Add fixture coverage for high-bit-depth CAVLC residuals, transform-bypass scans, and scaling-list metadata; current tests cover only small synthetic residual paths.

- CABAC residual and context blockers:
  - Validate full CABAC context initialization against known decoder traces beyond spot-check state tests.
  - Complete ctxIdxInc derivation for `mb_skip_flag`, `mb_type`, `sub_mb_type`, `ref_idx`, `mvd`, CBP, `transform_size_8x8_flag`, coded-block flags, significant/last-significant flags, `coeff_abs_level_minus1`, and bypass signs under FMO, MBAFF, field pictures, unavailable neighbors, and 4:2:2/4:4:4 chroma formats.
  - Make CABAC residual decoding exact for every block category, scan position, field/MBAFF mode, transform size, chroma format, and coded-block-neighbor condition.
  - Add tests that compare CABAC bin decisions, context state transitions, and decoded syntax elements against known-good traces.

- High-profile, chroma, and bit-depth blockers:
  - Verify exact 4:2:2 and 4:4:4 chroma DC/AC residual block order, macroblock coordinates, chroma QP values, bit-depth handling, transform-bypass state, and active scaling-list identifiers against high-profile StreamEye fixtures.
  - Decide whether residual output should include applied dequant/scaling values or remain syntax-only with scaling-list identifiers.
  - Handle separate colour planes as independent luma-style planes in macroblock prediction and residual output.

- Pixel-level parity, if visual reconstruction is required:
  - Implement inverse quantization, inverse transform, intra/inter prediction samples, reconstruction, DPB sample storage, and deblocking.
  - Keep this separate from syntax-number comparison so macroblock/residual/coefficient diffing remains useful before full pixel reconstruction exists.

## Current Macroblock/Entropy Boundary

The parser now records the bit offset at the end of each parsed slice header and enters `slice_data()`.

For CAVLC slices, I, P/SP, and B macroblocks now walk prediction syntax before CBP/residual consumption, including intra prediction modes, P L0 prediction syntax, B direct/L0/L1/bi-pred/B_8x8 syntax, coded-block-pattern mapping, optional transform 8x8, QP delta, I_PCM sample payloads, luma residual categories, and chroma DC/AC residual categories. B-slice `mb_skip_run` entries are reported as direct macroblocks with `direct_flag` and `direct_spatial_mv_pred_flag`.

For CABAC slices, the code byte-aligns after the slice header, initializes the CABAC arithmetic decoder, walks macroblocks until the terminate bin or picture bounds, and reports either skip state or macroblock type. The arithmetic engine now has the range-LPS table, MPS/LPS transitions, bypass bins, terminate bins, and table-driven context initialization for the syntax helpers currently consumed by the macroblock walker. The slice walker also keeps a per-macroblock neighbor-state map so `mb_skip_flag` uses left/top availability and skip state.

CABAC I-slice macroblocks now walk intra `mb_pred`, including Intra4x4/Intra8x8 prediction-mode flags and `intra_chroma_pred_mode`, before consuming CBP and QP-delta bins. Residual inspection is category-driven for luma 4x4, luma 8x8, Intra16x16 DC/AC, chroma DC, and chroma AC blocks. I_PCM macroblocks are byte-aligned and consumed using SPS bit-depth and chroma-format metadata.

P-slice inter macroblocks now walk L0 prediction syntax before CBP/residual consumption, including P_16x16, P_16x8, P_8x16, P_8x8, P_8x8ref0, sub-macroblock type, optional `ref_idx_l0`, and L0 motion-vector differences. Ref-index and MVD CABAC contexts use current-macroblock partition geometry plus stored left/top prediction state.

B-slice inter macroblocks now walk prediction syntax before CBP/residual consumption for L0, L1, bi-pred, direct, and B_8x8 forms. Direct partitions and B-skip are reported explicitly with `direct_flag` and `direct_spatial_mv_pred_flag`; they consume no explicit ref_idx/MVD syntax, matching the bitstream structure.

Unsupported decoded macroblock types are treated as parse failures instead of being skipped, so callers do not accidentally trust metadata from a desynchronized slice.
