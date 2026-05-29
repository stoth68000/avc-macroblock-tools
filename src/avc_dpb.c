#include "avc/avc_dpb.h"
#include <string.h>

void avc_dpb_init(avc_dpb_t *dpb)
{
    memset(dpb, 0, sizeof(*dpb));
}

static int32_t slice_poc(const avc_slice_header_t *slice)
{
    return (int32_t)slice->pic_order_cnt_lsb + slice->delta_pic_order_cnt_bottom;
}

static void sort_short_term_desc(avc_dpb_picture_t *pics, unsigned count)
{
    unsigned i;
    for (i = 1; i < count; i++) {
        avc_dpb_picture_t v = pics[i];
        unsigned j = i;
        while (j > 0 && pics[j - 1].frame_num < v.frame_num) {
            pics[j] = pics[j - 1];
            j--;
        }
        pics[j] = v;
    }
}

static void sort_short_term_asc(avc_dpb_picture_t *pics, unsigned count)
{
    unsigned i;
    for (i = 1; i < count; i++) {
        avc_dpb_picture_t v = pics[i];
        unsigned j = i;
        while (j > 0 && pics[j - 1].frame_num > v.frame_num) {
            pics[j] = pics[j - 1];
            j--;
        }
        pics[j] = v;
    }
}

static void sort_long_term_asc(avc_dpb_picture_t *pics, unsigned count)
{
    unsigned i;
    for (i = 1; i < count; i++) {
        avc_dpb_picture_t v = pics[i];
        unsigned j = i;
        while (j > 0 && pics[j - 1].long_term_frame_idx > v.long_term_frame_idx) {
            pics[j] = pics[j - 1];
            j--;
        }
        pics[j] = v;
    }
}

static void append_pic(avc_dpb_picture_t *dst, unsigned *count,
                       const avc_dpb_picture_t *pic)
{
    if (*count < AVC_REF_LIST_MAX) {
        dst[*count] = *pic;
        (*count)++;
    }
}

static void apply_list_modifications(avc_dpb_picture_t *list,
                                     unsigned *count,
                                     const avc_ref_pic_list_modification_t *mods,
                                     uint32_t mod_count,
                                     uint32_t frame_num)
{
    uint32_t i;
    for (i = 0; i < mod_count; i++) {
        uint32_t j;
        uint32_t target_frame_num = 0;

        if (mods[i].modification_of_pic_nums_idc == 0) {
            target_frame_num = frame_num - (mods[i].abs_diff_pic_num_minus1 + 1u);
        } else if (mods[i].modification_of_pic_nums_idc == 1) {
            target_frame_num = frame_num + (mods[i].abs_diff_pic_num_minus1 + 1u);
        }
        for (j = 0; j < *count; j++) {
            int match = 0;
            if (mods[i].modification_of_pic_nums_idc == 0 ||
                mods[i].modification_of_pic_nums_idc == 1) {
                match = !list[j].is_long_term && list[j].frame_num == target_frame_num;
            } else if (mods[i].modification_of_pic_nums_idc == 2) {
                match = list[j].is_long_term &&
                        list[j].long_term_frame_idx == mods[i].long_term_pic_num;
            }
            if (match && j != i && i < *count) {
                avc_dpb_picture_t pic = list[j];
                uint32_t k;
                if (j > i) {
                    for (k = j; k > i; k--) {
                        list[k] = list[k - 1];
                    }
                } else {
                    for (k = j; k + 1u < i; k++) {
                        list[k] = list[k + 1u];
                    }
                }
                list[i] = pic;
                break;
            }
        }
    }
}

void avc_dpb_build_ref_lists(const avc_dpb_t *dpb,
                             const avc_slice_header_t *slice,
                             avc_ref_list_state_t *lists)
{
    avc_dpb_picture_t short_refs[AVC_DPB_MAX_PICTURES];
    avc_dpb_picture_t long_refs[AVC_DPB_MAX_PICTURES];
    unsigned short_count = 0;
    unsigned long_count = 0;
    unsigned i;

    memset(lists, 0, sizeof(*lists));
    for (i = 0; i < dpb->count; i++) {
        if (!dpb->pictures[i].valid) {
            continue;
        }
        if (dpb->pictures[i].is_long_term) {
            long_refs[long_count++] = dpb->pictures[i];
        } else {
            short_refs[short_count++] = dpb->pictures[i];
        }
    }

    sort_long_term_asc(long_refs, long_count);
    if (slice->slice_kind == AVC_SLICE_B) {
        sort_short_term_asc(short_refs, short_count);
    } else {
        sort_short_term_desc(short_refs, short_count);
    }
    for (i = 0; i < short_count; i++) {
        append_pic(lists->l0, &lists->count_l0, &short_refs[i]);
    }
    for (i = 0; i < long_count; i++) {
        append_pic(lists->l0, &lists->count_l0, &long_refs[i]);
    }
    if (slice->slice_kind == AVC_SLICE_B) {
        sort_short_term_desc(short_refs, short_count);
        for (i = 0; i < short_count; i++) {
            append_pic(lists->l1, &lists->count_l1, &short_refs[i]);
        }
        for (i = 0; i < long_count; i++) {
            append_pic(lists->l1, &lists->count_l1, &long_refs[i]);
        }
    }

    if (slice->ref_pic_list_modification_flag_l0) {
        apply_list_modifications(lists->l0, &lists->count_l0,
                                 slice->ref_pic_list_modifications_l0,
                                 slice->ref_pic_list_modification_count_l0,
                                 slice->frame_num);
    }
    if (slice->ref_pic_list_modification_flag_l1) {
        apply_list_modifications(lists->l1, &lists->count_l1,
                                 slice->ref_pic_list_modifications_l1,
                                 slice->ref_pic_list_modification_count_l1,
                                 slice->frame_num);
    }
}

static void remove_by_frame_num(avc_dpb_t *dpb, uint32_t frame_num)
{
    unsigned i;
    for (i = 0; i < dpb->count; i++) {
        if (dpb->pictures[i].valid && !dpb->pictures[i].is_long_term &&
            dpb->pictures[i].frame_num == frame_num) {
            dpb->pictures[i].valid = 0;
        }
    }
}

static void remove_by_long_term(avc_dpb_t *dpb, uint32_t long_term_pic_num)
{
    unsigned i;
    for (i = 0; i < dpb->count; i++) {
        if (dpb->pictures[i].valid && dpb->pictures[i].is_long_term &&
            dpb->pictures[i].long_term_frame_idx == long_term_pic_num) {
            dpb->pictures[i].valid = 0;
        }
    }
}

static avc_dpb_picture_t *find_short_term(avc_dpb_t *dpb, uint32_t frame_num)
{
    unsigned i;
    for (i = 0; i < dpb->count; i++) {
        if (dpb->pictures[i].valid && !dpb->pictures[i].is_long_term &&
            dpb->pictures[i].frame_num == frame_num) {
            return &dpb->pictures[i];
        }
    }
    return NULL;
}

static void compact_dpb(avc_dpb_t *dpb)
{
    unsigned read;
    unsigned write = 0;
    for (read = 0; read < dpb->count; read++) {
        if (dpb->pictures[read].valid) {
            if (write != read) {
                dpb->pictures[write] = dpb->pictures[read];
            }
            write++;
        }
    }
    dpb->count = write;
}

static void append_current_picture(avc_dpb_t *dpb,
                                   const avc_slice_header_t *slice)
{
    avc_dpb_picture_t pic;

    if (slice->nal.nal_ref_idc == 0) {
        return;
    }
    compact_dpb(dpb);
    if (dpb->count >= AVC_DPB_MAX_PICTURES) {
        memmove(&dpb->pictures[0], &dpb->pictures[1],
                sizeof(dpb->pictures[0]) * (AVC_DPB_MAX_PICTURES - 1u));
        dpb->count = AVC_DPB_MAX_PICTURES - 1u;
    }
    pic = (avc_dpb_picture_t){0};
    pic.valid = 1;
    pic.frame_num = slice->frame_num;
    pic.poc = slice_poc(slice);
    pic.is_idr = slice->nal.nal_unit_type == AVC_NAL_SLICE_IDR;
    pic.is_long_term = slice->long_term_reference_flag;
    pic.long_term_frame_idx = slice->long_term_reference_flag ? 0u : 0xffffffffu;
    dpb->pictures[dpb->count++] = pic;
}

void avc_dpb_finish_slice(avc_dpb_t *dpb,
                          const avc_slice_header_t *slice,
                          const avc_sps_t *sps)
{
    uint32_t i;
    uint32_t max_refs = sps->max_num_ref_frames ? sps->max_num_ref_frames : AVC_DPB_MAX_PICTURES;

    if (slice->nal.nal_unit_type == AVC_NAL_SLICE_IDR) {
        avc_dpb_init(dpb);
        append_current_picture(dpb, slice);
        return;
    }

    for (i = 0; i < slice->dec_ref_pic_marking_count; i++) {
        const avc_dec_ref_pic_marking_t *m = &slice->dec_ref_pic_marking[i];
        if (m->memory_management_control_operation == 1) {
            uint32_t frame_num = slice->frame_num - (m->difference_of_pic_nums_minus1 + 1u);
            remove_by_frame_num(dpb, frame_num);
        } else if (m->memory_management_control_operation == 2) {
            remove_by_long_term(dpb, m->long_term_pic_num);
        } else if (m->memory_management_control_operation == 3) {
            avc_dpb_picture_t *pic;
            uint32_t frame_num = slice->frame_num - (m->difference_of_pic_nums_minus1 + 1u);
            pic = find_short_term(dpb, frame_num);
            if (pic) {
                pic->is_long_term = 1;
                pic->long_term_frame_idx = m->long_term_frame_idx;
            }
        } else if (m->memory_management_control_operation == 5) {
            avc_dpb_init(dpb);
        }
    }

    append_current_picture(dpb, slice);
    compact_dpb(dpb);
    while (dpb->count > max_refs && dpb->count > 0) {
        memmove(&dpb->pictures[0], &dpb->pictures[1],
                sizeof(dpb->pictures[0]) * (dpb->count - 1u));
        dpb->count--;
    }
}
