#ifndef AVC_MACROBLOCK_H
#define AVC_MACROBLOCK_H

#include <stddef.h>
#include <stdint.h>
#include "avc/avc_cabac.h"
#include "avc/avc_cavlc.h"
#include "avc/avc_syntax.h"

#define AVC_MB_PRED_MAX_PARTITIONS 4
#define AVC_MB_PRED_MAX_SUB_PARTITIONS 16

typedef enum {
    AVC_MB_ENTROPY_CAVLC = 0,
    AVC_MB_ENTROPY_CABAC = 1
} avc_mb_entropy_t;

typedef struct {
    uint32_t address;
    uint32_t mb_type;
    uint32_t mb_skip_run;
    uint32_t coded_block_pattern_luma;
    uint32_t coded_block_pattern_chroma;
    int32_t mb_qp_delta;
    int32_t qp_y;
    int32_t qp_cb;
    int32_t qp_cr;
    int skipped;
    int mb_field_decoding_flag;
    int transform_size_8x8_flag;
    int pcm_sample_bits_luma;
    int pcm_sample_bits_chroma;
    uint32_t pcm_luma_samples;
    uint32_t pcm_chroma_samples;
    avc_mb_entropy_t entropy;
} avc_macroblock_event_t;

typedef enum {
    AVC_MB_PRED_UNKNOWN = 0,
    AVC_MB_PRED_INTRA_4X4 = 1,
    AVC_MB_PRED_INTRA_8X8 = 2,
    AVC_MB_PRED_INTRA_16X16 = 3,
    AVC_MB_PRED_INTER = 4,
    AVC_MB_PRED_PCM = 5
} avc_mb_pred_kind_t;

typedef struct {
    uint32_t mb_address;
    avc_mb_pred_kind_t kind;
    unsigned intra_chroma_pred_mode;
    unsigned partition_count;
    uint8_t partition_x[AVC_MB_PRED_MAX_PARTITIONS];
    uint8_t partition_y[AVC_MB_PRED_MAX_PARTITIONS];
    uint8_t partition_width[AVC_MB_PRED_MAX_PARTITIONS];
    uint8_t partition_height[AVC_MB_PRED_MAX_PARTITIONS];
    uint8_t sub_partition_count[AVC_MB_PRED_MAX_PARTITIONS];
    uint8_t sub_partition_width[AVC_MB_PRED_MAX_PARTITIONS];
    uint8_t sub_partition_height[AVC_MB_PRED_MAX_PARTITIONS];
    uint8_t list_mask[AVC_MB_PRED_MAX_PARTITIONS];
    uint8_t direct_flag[AVC_MB_PRED_MAX_PARTITIONS];
    uint8_t direct_spatial_mv_pred_flag;
    unsigned sub_mb_type[AVC_MB_PRED_MAX_PARTITIONS];
    unsigned ref_idx_l0[AVC_MB_PRED_MAX_PARTITIONS];
    unsigned ref_idx_l1[AVC_MB_PRED_MAX_PARTITIONS];
    int16_t mvd_l0[AVC_MB_PRED_MAX_PARTITIONS][2];
    int16_t mvd_l1[AVC_MB_PRED_MAX_PARTITIONS][2];
    int16_t mv_pred_l0[AVC_MB_PRED_MAX_PARTITIONS][2];
    int16_t mv_pred_l1[AVC_MB_PRED_MAX_PARTITIONS][2];
    int16_t mv_l0[AVC_MB_PRED_MAX_PARTITIONS][2];
    int16_t mv_l1[AVC_MB_PRED_MAX_PARTITIONS][2];
    unsigned sub_partition_total;
    uint8_t sub_partition_parent[AVC_MB_PRED_MAX_SUB_PARTITIONS];
    uint8_t sub_part_x[AVC_MB_PRED_MAX_SUB_PARTITIONS];
    uint8_t sub_part_y[AVC_MB_PRED_MAX_SUB_PARTITIONS];
    uint8_t sub_part_width[AVC_MB_PRED_MAX_SUB_PARTITIONS];
    uint8_t sub_part_height[AVC_MB_PRED_MAX_SUB_PARTITIONS];
    uint8_t sub_part_list_mask[AVC_MB_PRED_MAX_SUB_PARTITIONS];
    uint8_t sub_part_direct_flag[AVC_MB_PRED_MAX_SUB_PARTITIONS];
    unsigned sub_part_ref_idx_l0[AVC_MB_PRED_MAX_SUB_PARTITIONS];
    unsigned sub_part_ref_idx_l1[AVC_MB_PRED_MAX_SUB_PARTITIONS];
    int16_t sub_part_mvd_l0[AVC_MB_PRED_MAX_SUB_PARTITIONS][2];
    int16_t sub_part_mvd_l1[AVC_MB_PRED_MAX_SUB_PARTITIONS][2];
    int16_t sub_part_mv_pred_l0[AVC_MB_PRED_MAX_SUB_PARTITIONS][2];
    int16_t sub_part_mv_pred_l1[AVC_MB_PRED_MAX_SUB_PARTITIONS][2];
    int16_t sub_part_mv_l0[AVC_MB_PRED_MAX_SUB_PARTITIONS][2];
    int16_t sub_part_mv_l1[AVC_MB_PRED_MAX_SUB_PARTITIONS][2];
    uint8_t prev_intra_pred_mode_flag[16];
    uint8_t rem_intra_pred_mode[16];
} avc_mb_pred_event_t;

typedef enum {
    AVC_RESIDUAL_LUMA_4X4 = 0,
    AVC_RESIDUAL_LUMA_8X8 = 1,
    AVC_RESIDUAL_LUMA_16X16_DC = 2,
    AVC_RESIDUAL_LUMA_16X16_AC = 3,
    AVC_RESIDUAL_CHROMA_DC = 4,
    AVC_RESIDUAL_CHROMA_AC = 5
} avc_residual_kind_t;

typedef struct {
    uint32_t mb_address;
    avc_residual_kind_t block_kind;
    unsigned block_index;
    avc_mb_entropy_t entropy;
    uint8_t component; /* 0=Y, 1=Cb, 2=Cr */
    uint8_t chroma_format_idc;
    uint8_t chroma_array_type;
    uint8_t separate_colour_plane_flag;
    uint8_t bit_depth_luma;
    uint8_t bit_depth_chroma;
    int32_t qp_y;
    int32_t qp_cb;
    int32_t qp_cr;
    int32_t qp_for_block;
    uint8_t transform_bypass;
    uint8_t scaling_list_size;
    int8_t scaling_list_index;
    uint8_t scaling_list_present_flag;
    uint8_t scaling_list_use_default_flag;
    unsigned coeff_mb_x[AVC_CAVLC_MAX_COEFFS];
    unsigned coeff_mb_y[AVC_CAVLC_MAX_COEFFS];
    avc_cavlc_block_t block;
    avc_cabac_residual_block_t cabac_block;
} avc_residual_event_t;

typedef struct {
    void (*on_macroblock)(void *opaque, const avc_macroblock_event_t *mb);
    void (*on_mb_pred)(void *opaque, const avc_mb_pred_event_t *pred);
    void (*on_residual)(void *opaque, const avc_residual_event_t *residual);
    void (*on_note)(void *opaque, const char *message);
} avc_macroblock_callbacks_t;

typedef struct {
    uint32_t first_mb_in_slice;
    uint32_t next_mb_address;
    uint32_t max_macroblocks;
    uint32_t macroblocks_seen;
    uint8_t entropy_coding_mode_flag;
    int cabac_initialized;
    int complete;
    int picture_complete;
} avc_slice_data_summary_t;

int avc_parse_slice_data(const uint8_t *rbsp, size_t rbsp_size,
                         const avc_slice_header_t *slice,
                         const avc_parameter_sets_t *sets,
                         avc_macroblock_callbacks_t callbacks,
                         void *opaque,
                         avc_slice_data_summary_t *summary);

#endif
