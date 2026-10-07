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
    uint8_t coded_block_flag_present;
    uint8_t coded_block_flag;
    unsigned error_index;
    unsigned error_context;
    int error_syntax;
    int coeff_level[64];
    unsigned coeff_x[64];
    unsigned coeff_y[64];
    uint8_t significant[64];
    uint8_t last_significant[64];
} avc_cabac_residual_block_t;

typedef enum {
    AVC_CABAC_RESIDUAL_ERROR_NONE = 0,
    AVC_CABAC_RESIDUAL_ERROR_CODED_BLOCK_FLAG,
    AVC_CABAC_RESIDUAL_ERROR_SIGNIFICANT_COEFF_FLAG,
    AVC_CABAC_RESIDUAL_ERROR_LAST_SIGNIFICANT_COEFF_FLAG,
    AVC_CABAC_RESIDUAL_ERROR_COEFF_ABS_LEVEL_MINUS1,
    AVC_CABAC_RESIDUAL_ERROR_COEFF_SIGN_FLAG,
    AVC_CABAC_RESIDUAL_ERROR_COEFF_SCAN_EXHAUSTED,
    AVC_CABAC_RESIDUAL_ERROR_INVALID_MAX_COEFF
} avc_cabac_residual_error_t;

typedef void (*avc_cabac_residual_trace_fn)(void *opaque,
                                            const char *syntax,
                                            unsigned ctx_idx,
                                            int bin,
                                            unsigned scan_index,
                                            size_t bit_pos,
                                            uint32_t cod_i_range,
                                            uint32_t cod_i_offset);

void avc_cabac_init(avc_cabac_decoder_t *cabac, const uint8_t *data, size_t size);
int avc_cabac_init_contexts(avc_cabac_decoder_t *cabac, unsigned slice_qp_y,
                            unsigned cabac_init_idc, unsigned slice_type);
int avc_cabac_decode_bypass(avc_cabac_decoder_t *cabac);
int avc_cabac_decode_terminate(avc_cabac_decoder_t *cabac);
void avc_cabac_continue_after_nonterminal_terminate(avc_cabac_decoder_t *cabac);
int avc_cabac_decode_decision(avc_cabac_decoder_t *cabac, unsigned ctx_idx);
void avc_cabac_byte_align(avc_cabac_decoder_t *cabac);
uint32_t avc_cabac_read_pcm_bits(avc_cabac_decoder_t *cabac, unsigned n);
void avc_cabac_set_context(avc_cabac_decoder_t *cabac, unsigned ctx_idx,
                           uint8_t state, uint8_t mps);
unsigned avc_cabac_ctx_coded_block_pattern_luma(unsigned bin_idx,
                                                int left_available, unsigned left_cbp_luma,
                                                int top_available, unsigned top_cbp_luma,
                                                unsigned prior_cbp_luma);
unsigned avc_cabac_ctx_coded_block_pattern_chroma(unsigned bin_idx,
                                                  int left_available, unsigned left_cbp_chroma,
                                                  int top_available, unsigned top_cbp_chroma);
unsigned avc_cabac_ctx_transform_size_8x8(int left_transform_8x8,
                                          int top_transform_8x8);
unsigned avc_cabac_ctx_mb_qp_delta(int previous_mb_qp_delta_nonzero);
unsigned avc_cabac_ctx_mb_field_decoding_flag(int left_available, int left_field,
                                              int top_available, int top_field);
unsigned avc_cabac_ctx_ref_idx(int left_nonzero, int top_nonzero);
unsigned avc_cabac_ctx_mvd(unsigned abs_mvd_left, unsigned abs_mvd_top);
unsigned avc_cabac_ctx_coded_block_flag(int left_coded, int top_coded);
unsigned avc_cabac_ctx_residual_flag(unsigned ctx_block_cat,
                                     unsigned scan_index,
                                     unsigned max_coeff,
                                     int field_scan,
                                     int last_significant);
int avc_cabac_decode_mb_skip_flag(avc_cabac_decoder_t *cabac, unsigned slice_type,
                                  int left_available, int left_skipped,
                                  int top_available, int top_skipped);
int avc_cabac_decode_mb_type_i(avc_cabac_decoder_t *cabac, unsigned *mb_type);
int avc_cabac_decode_mb_type_p(avc_cabac_decoder_t *cabac, unsigned *mb_type);
int avc_cabac_decode_mb_type_b(avc_cabac_decoder_t *cabac,
                               int left_available, int left_direct,
                               int top_available, int top_direct,
                               unsigned *mb_type);
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
int avc_cabac_decode_mb_qp_delta_traced(avc_cabac_decoder_t *cabac,
                                        int previous_mb_qp_delta_nonzero,
                                        int *mb_qp_delta,
                                        void (*on_trace)(void *opaque,
                                                         unsigned prefix,
                                                         unsigned ctx_idx,
                                                         int bin,
                                                         size_t bit_pos,
                                                         uint32_t cod_i_range,
                                                         uint32_t cod_i_offset),
                                        void *opaque);
int avc_cabac_decode_prev_intra_pred_mode_flag(avc_cabac_decoder_t *cabac);
int avc_cabac_decode_rem_intra_pred_mode(avc_cabac_decoder_t *cabac,
                                         unsigned *mode);
int avc_cabac_decode_intra_chroma_pred_mode(avc_cabac_decoder_t *cabac,
                                            unsigned *mode);
int avc_cabac_decode_mb_field_decoding_flag(avc_cabac_decoder_t *cabac,
                                            int left_available, int left_field,
                                            int top_available, int top_field);
int avc_cabac_decode_ref_idx_l0(avc_cabac_decoder_t *cabac,
                                int left_nonzero, int top_nonzero,
                                unsigned *ref_idx);
int avc_cabac_decode_ref_idx_l1(avc_cabac_decoder_t *cabac,
                                int left_nonzero, int top_nonzero,
                                unsigned *ref_idx);
int avc_cabac_decode_ref_idx_l0_bounded(avc_cabac_decoder_t *cabac,
                                        int left_nonzero, int top_nonzero,
                                        unsigned max_ref_idx,
                                        unsigned *ref_idx);
int avc_cabac_decode_ref_idx_l1_bounded(avc_cabac_decoder_t *cabac,
                                        int left_nonzero, int top_nonzero,
                                        unsigned max_ref_idx,
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
                                            unsigned ctx_base,
                                            unsigned ctx_block_cat,
                                            unsigned scan_index,
                                            unsigned max_coeff,
                                            int field_scan);
int avc_cabac_decode_last_significant_coeff_flag(avc_cabac_decoder_t *cabac,
                                                 unsigned ctx_base,
                                                 unsigned ctx_block_cat,
                                                 unsigned scan_index,
                                                 unsigned max_coeff,
                                                 int field_scan);
int avc_cabac_decode_coeff_abs_level_minus1(avc_cabac_decoder_t *cabac,
                                            unsigned ctx_base, unsigned *value);
int avc_cabac_decode_coeff_abs_level_minus1_stateful(avc_cabac_decoder_t *cabac,
                                                     unsigned ctx_base,
                                                     unsigned ctx_block_cat,
                                                     unsigned num_abs_level_eq1,
                                                     unsigned num_abs_level_gt1,
                                                     unsigned *value);
int avc_cabac_decode_residual_block(avc_cabac_decoder_t *cabac,
                                    unsigned max_coeff,
                                    unsigned ctx_block_cat,
                                    int coded_block_flag_present,
                                    unsigned coded_ctx_base,
                                    unsigned sig_ctx_base,
                                    unsigned last_ctx_base,
                                    unsigned level_ctx_base,
                                    int left_coded,
                                    int top_coded,
                                    int field_scan,
                                    avc_cabac_residual_block_t *block);
int avc_cabac_decode_residual_block_traced(avc_cabac_decoder_t *cabac,
                                           unsigned max_coeff,
                                           unsigned ctx_block_cat,
                                           int coded_block_flag_present,
                                           unsigned coded_ctx_base,
                                           unsigned sig_ctx_base,
                                           unsigned last_ctx_base,
                                           unsigned level_ctx_base,
                                           int left_coded,
                                           int top_coded,
                                           int field_scan,
                                           avc_cabac_residual_block_t *block,
                                           avc_cabac_residual_trace_fn on_trace,
                                           void *trace_opaque);

#endif
