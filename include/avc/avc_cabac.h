#ifndef AVC_CABAC_H
#define AVC_CABAC_H

#include <stddef.h>
#include <stdint.h>

#define AVC_CABAC_CONTEXTS 1024

typedef struct {
    uint8_t state;
    uint8_t mps;
} avc_cabac_context_t;

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t bit_pos;
    uint32_t cod_i_range;
    uint32_t cod_i_offset;
    int error;
    avc_cabac_context_t ctx[AVC_CABAC_CONTEXTS];
} avc_cabac_decoder_t;

typedef struct {
    unsigned max_coeff;
    unsigned total_coeff;
    int coeff_level[64];
    uint8_t significant[64];
    uint8_t last_significant[64];
} avc_cabac_residual_block_t;

void avc_cabac_init(avc_cabac_decoder_t *cabac, const uint8_t *data, size_t size);
int avc_cabac_init_contexts(avc_cabac_decoder_t *cabac, unsigned slice_qp_y,
                            unsigned cabac_init_idc, unsigned slice_type);
int avc_cabac_decode_bypass(avc_cabac_decoder_t *cabac);
int avc_cabac_decode_terminate(avc_cabac_decoder_t *cabac);
int avc_cabac_decode_decision(avc_cabac_decoder_t *cabac, unsigned ctx_idx);
void avc_cabac_byte_align(avc_cabac_decoder_t *cabac);
uint32_t avc_cabac_read_pcm_bits(avc_cabac_decoder_t *cabac, unsigned n);
void avc_cabac_set_context(avc_cabac_decoder_t *cabac, unsigned ctx_idx,
                           uint8_t state, uint8_t mps);
int avc_cabac_decode_mb_skip_flag(avc_cabac_decoder_t *cabac, unsigned slice_type,
                                  int left_available, int left_skipped,
                                  int top_available, int top_skipped);
int avc_cabac_decode_mb_type_i(avc_cabac_decoder_t *cabac, unsigned *mb_type);
int avc_cabac_decode_mb_type_p(avc_cabac_decoder_t *cabac, unsigned *mb_type);
int avc_cabac_decode_mb_type_b(avc_cabac_decoder_t *cabac, unsigned *mb_type);
int avc_cabac_decode_transform_size_8x8_flag(avc_cabac_decoder_t *cabac,
                                             int left_transform_8x8,
                                             int top_transform_8x8);
int avc_cabac_decode_coded_block_pattern_luma(avc_cabac_decoder_t *cabac,
                                              int left_available, unsigned left_cbp_luma,
                                              int top_available, unsigned top_cbp_luma,
                                              unsigned *coded_block_pattern_luma);
int avc_cabac_decode_coded_block_pattern_chroma(avc_cabac_decoder_t *cabac,
                                                int left_available, unsigned left_cbp_chroma,
                                                int top_available, unsigned top_cbp_chroma,
                                                unsigned *coded_block_pattern_chroma);
int avc_cabac_decode_mb_qp_delta(avc_cabac_decoder_t *cabac,
                                 int previous_mb_qp_delta_nonzero,
                                 int *mb_qp_delta);
int avc_cabac_decode_prev_intra_pred_mode_flag(avc_cabac_decoder_t *cabac);
int avc_cabac_decode_rem_intra_pred_mode(avc_cabac_decoder_t *cabac,
                                         unsigned *mode);
int avc_cabac_decode_intra_chroma_pred_mode(avc_cabac_decoder_t *cabac,
                                            unsigned *mode);
int avc_cabac_decode_ref_idx_l0(avc_cabac_decoder_t *cabac,
                                int left_nonzero, int top_nonzero,
                                unsigned *ref_idx);
int avc_cabac_decode_ref_idx_l1(avc_cabac_decoder_t *cabac,
                                int left_nonzero, int top_nonzero,
                                unsigned *ref_idx);
int avc_cabac_decode_mvd_component(avc_cabac_decoder_t *cabac,
                                   unsigned ctx_base,
                                   unsigned abs_mvd_left,
                                   unsigned abs_mvd_top,
                                   int16_t *mvd);
int avc_cabac_decode_sub_mb_type_p(avc_cabac_decoder_t *cabac,
                                   unsigned *sub_mb_type);
int avc_cabac_decode_sub_mb_type_b(avc_cabac_decoder_t *cabac,
                                   unsigned *sub_mb_type);
int avc_cabac_decode_coded_block_flag(avc_cabac_decoder_t *cabac,
                                      unsigned ctx_base,
                                      int left_coded,
                                      int top_coded);
int avc_cabac_decode_significant_coeff_flag(avc_cabac_decoder_t *cabac,
                                            unsigned ctx_base, unsigned scan_index);
int avc_cabac_decode_last_significant_coeff_flag(avc_cabac_decoder_t *cabac,
                                                 unsigned ctx_base, unsigned scan_index);
int avc_cabac_decode_coeff_abs_level_minus1(avc_cabac_decoder_t *cabac,
                                            unsigned ctx_base, unsigned *value);
int avc_cabac_decode_coeff_abs_level_minus1_stateful(avc_cabac_decoder_t *cabac,
                                                     unsigned ctx_base,
                                                     unsigned num_abs_level_eq1,
                                                     unsigned num_abs_level_gt1,
                                                     unsigned *value);
int avc_cabac_decode_residual_block(avc_cabac_decoder_t *cabac,
                                    unsigned max_coeff,
                                    unsigned coded_ctx_base,
                                    unsigned sig_ctx_base,
                                    unsigned last_ctx_base,
                                    unsigned level_ctx_base,
                                    int left_coded,
                                    int top_coded,
                                    avc_cabac_residual_block_t *block);

#endif
