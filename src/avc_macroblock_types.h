#ifndef AVC_MACROBLOCK_TYPES_H
#define AVC_MACROBLOCK_TYPES_H

#include <stdint.h>
#include "avc/avc_macroblock.h"

typedef struct {
    avc_mb_pred_kind_t pred_kind;
    uint32_t cbp_luma;
    uint32_t cbp_chroma;
    int is_pcm;
} avc_i_mb_type_info_t;

typedef enum {
    AVC_P_MB_UNKNOWN = 0,
    AVC_P_MB_L0_16X16,
    AVC_P_MB_L0_L0_16X8,
    AVC_P_MB_L0_L0_8X16,
    AVC_P_MB_8X8,
    AVC_P_MB_8X8REF0,
    AVC_P_MB_INTRA
} avc_p_mb_shape_t;

typedef struct {
    avc_p_mb_shape_t shape;
    avc_i_mb_type_info_t intra;
} avc_p_mb_type_info_t;

typedef enum {
    AVC_B_MB_UNKNOWN = 0,
    AVC_B_MB_DIRECT,
    AVC_B_MB_INTER,
    AVC_B_MB_8X8,
    AVC_B_MB_INTRA
} avc_b_mb_shape_t;

typedef struct {
    avc_b_mb_shape_t shape;
    unsigned partition_count;
    uint8_t list_mask[4];
    uint8_t partition_width[4];
    uint8_t partition_height[4];
    avc_i_mb_type_info_t intra;
} avc_b_mb_type_info_t;

avc_i_mb_type_info_t avc_i_mb_type_classify(uint32_t mb_type);
avc_p_mb_type_info_t avc_p_mb_type_classify(uint32_t mb_type);
avc_b_mb_type_info_t avc_b_mb_type_classify(uint32_t mb_type);

unsigned avc_p_inter_partition_count(avc_p_mb_shape_t shape);
uint8_t avc_b_sub_mb_list_mask(unsigned sub_mb_type);
int avc_b_sub_mb_is_direct(unsigned sub_mb_type);

void avc_mb_set_partition_geometry(avc_mb_pred_event_t *pred,
                                   unsigned partition,
                                   unsigned x,
                                   unsigned y,
                                   unsigned width,
                                   unsigned height,
                                   unsigned sub_count,
                                   unsigned sub_width,
                                   unsigned sub_height);
void avc_p_mb_set_partition_geometry(avc_mb_pred_event_t *pred,
                                     avc_p_mb_shape_t shape);
void avc_b_mb_set_partition_geometry(avc_mb_pred_event_t *pred,
                                     const avc_b_mb_type_info_t *info);
void avc_mb_populate_sub_partition_motion(avc_mb_pred_event_t *pred);

void avc_p_mb_partition_neighbors(avc_p_mb_shape_t shape, unsigned partition,
                                  int *left_current, unsigned *left_partition,
                                  int *top_current, unsigned *top_partition,
                                  int *top_right_current,
                                  unsigned *top_right_partition);

#endif
