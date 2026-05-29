#ifndef AVC_DPB_H
#define AVC_DPB_H

#include <stdint.h>
#include "avc/avc_syntax.h"

#define AVC_DPB_MAX_PICTURES 32
#define AVC_REF_LIST_MAX 32

typedef struct {
    uint32_t frame_num;
    int32_t poc;
    uint8_t is_long_term;
    uint32_t long_term_frame_idx;
    uint8_t is_idr;
    uint8_t valid;
} avc_dpb_picture_t;

typedef struct {
    unsigned count_l0;
    unsigned count_l1;
    avc_dpb_picture_t l0[AVC_REF_LIST_MAX];
    avc_dpb_picture_t l1[AVC_REF_LIST_MAX];
} avc_ref_list_state_t;

typedef struct {
    unsigned count;
    avc_dpb_picture_t pictures[AVC_DPB_MAX_PICTURES];
} avc_dpb_t;

void avc_dpb_init(avc_dpb_t *dpb);
void avc_dpb_build_ref_lists(const avc_dpb_t *dpb,
                             const avc_slice_header_t *slice,
                             avc_ref_list_state_t *lists);
void avc_dpb_finish_slice(avc_dpb_t *dpb,
                          const avc_slice_header_t *slice,
                          const avc_sps_t *sps);

#endif
