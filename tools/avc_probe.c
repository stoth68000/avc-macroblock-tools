#include "avc/avc_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>

typedef struct
{
    int valid;
    uint8_t nal_ref_idc;
    uint8_t nal_unit_type;
    uint32_t frame_num;
    uint32_t pic_parameter_set_id;
    uint8_t field_pic_flag;
    uint8_t bottom_field_flag;
    uint32_t idr_pic_id;
    uint32_t pic_order_cnt_lsb;
    int32_t delta_pic_order_cnt_bottom;
    int32_t delta_pic_order_cnt0;
    int32_t delta_pic_order_cnt1;
} probe_picture_key_t;

typedef struct
{
    const char *input;
    int filter_mb;
    uint32_t mb_address;
    int filter_picture;
    uint32_t picture_index;
    int filter_slice;
    uint32_t slice_index;
    int filter_slice_in_picture;
    uint32_t slice_in_picture;
    uint32_t current_nal_index;
    size_t current_nal_offset;
    uint8_t current_nal_type;
    uint32_t nal_count;
    uint32_t current_picture_index;
    uint32_t current_slice_index;
    uint32_t current_slice_in_picture;
    uint32_t picture_count;
    uint32_t slice_count;
    uint32_t slices_in_current_picture;
    probe_picture_key_t previous_picture;
    int have_ref_lists;
    avc_ref_list_state_t ref_lists;
} probe_options_t;

static int want_current_slice(const probe_options_t *options)
{
    if (options->filter_picture &&
        options->current_picture_index != options->picture_index) {
        return 0;
    }
    if (options->filter_slice &&
        options->current_slice_index != options->slice_index) {
        return 0;
    }
    if (options->filter_slice_in_picture &&
        options->current_slice_in_picture != options->slice_in_picture) {
        return 0;
    }
    return 1;
}

static int want_mb(const probe_options_t *options, uint32_t mb_address)
{
    if (!want_current_slice(options)) {
        return 0;
    }
    return !options->filter_mb || options->mb_address == mb_address;
}

static probe_picture_key_t picture_key_from_slice(const avc_slice_header_t *slice)
{
    probe_picture_key_t key;

    key = (probe_picture_key_t){0};
    key.valid = 1;
    key.nal_ref_idc = slice->nal.nal_ref_idc;
    key.nal_unit_type = slice->nal.nal_unit_type;
    key.frame_num = slice->frame_num;
    key.pic_parameter_set_id = slice->pic_parameter_set_id;
    key.field_pic_flag = slice->field_pic_flag;
    key.bottom_field_flag = slice->bottom_field_flag;
    key.idr_pic_id = slice->idr_pic_id;
    key.pic_order_cnt_lsb = slice->pic_order_cnt_lsb;
    key.delta_pic_order_cnt_bottom = slice->delta_pic_order_cnt_bottom;
    key.delta_pic_order_cnt0 = slice->delta_pic_order_cnt[0];
    key.delta_pic_order_cnt1 = slice->delta_pic_order_cnt[1];
    return key;
}

static int starts_new_picture(const probe_picture_key_t *previous,
                              const probe_picture_key_t *current)
{
    if (!previous->valid) {
        return 1;
    }
    if (previous->frame_num != current->frame_num ||
        previous->pic_parameter_set_id != current->pic_parameter_set_id ||
        previous->field_pic_flag != current->field_pic_flag ||
        previous->bottom_field_flag != current->bottom_field_flag ||
        previous->nal_unit_type != current->nal_unit_type ||
        previous->idr_pic_id != current->idr_pic_id ||
        previous->pic_order_cnt_lsb != current->pic_order_cnt_lsb ||
        previous->delta_pic_order_cnt_bottom != current->delta_pic_order_cnt_bottom ||
        previous->delta_pic_order_cnt0 != current->delta_pic_order_cnt0 ||
        previous->delta_pic_order_cnt1 != current->delta_pic_order_cnt1) {
        return 1;
    }
    if ((previous->nal_ref_idc == 0) != (current->nal_ref_idc == 0)) {
        return 1;
    }
    return 0;
}

static void on_nal(void *opaque, const avc_nal_unit_t *nal)
{
    probe_options_t *options = (probe_options_t *)opaque;

    options->current_nal_index = options->nal_count++;
    options->current_nal_offset = nal->offset;
    options->current_nal_type = nal->header.nal_unit_type;
    printf("{\"event\":\"nal\",\"nal_index\":%u,\"offset\":%zu,\"type\":%u,\"type_name\":\"%s\",\"nal_ref_idc\":%u,\"ebsp_size\":%zu}\n",
           options->current_nal_index, nal->offset, nal->header.nal_unit_type, avc_nal_type_name(nal->header.nal_unit_type),
           nal->header.nal_ref_idc, nal->ebsp_size);
}

static void on_sps(void *opaque, const avc_sps_t *sps)
{
    const probe_options_t *options = (const probe_options_t *)opaque;

    printf("{\"event\":\"sps\",\"nal_index\":%u,\"id\":%u,\"profile_idc\":%u,\"level_idc\":%u,\"width\":%u,\"height\":%u,\"max_num_ref_frames\":%u,\"frame_mbs_only\":%u,\"mbaff\":%u,\"vui_present\":%u}\n",
           options->current_nal_index, sps->seq_parameter_set_id, sps->profile_idc, sps->level_idc,
           avc_sps_width(sps), avc_sps_height(sps), sps->max_num_ref_frames,
           sps->frame_mbs_only_flag, sps->mb_adaptive_frame_field_flag,
           sps->vui_parameters_present_flag);
}

static void on_pps(void *opaque, const avc_pps_t *pps)
{
    const probe_options_t *options = (const probe_options_t *)opaque;

    printf("{\"event\":\"pps\",\"nal_index\":%u,\"id\":%u,\"sps_id\":%u,\"cabac\":%u,\"slice_groups_minus1\":%u,\"slice_group_map_type\":%u,\"pic_scaling_matrix_present\":%u,\"deblocking_control\":%u,\"transform_8x8\":%u,\"second_chroma_qp_index_offset\":%d}\n",
           options->current_nal_index, pps->pic_parameter_set_id, pps->seq_parameter_set_id,
           pps->entropy_coding_mode_flag, pps->num_slice_groups_minus1,
           pps->slice_group_map_type, pps->pic_scaling_matrix_present_flag,
           pps->deblocking_filter_control_present_flag, pps->transform_8x8_mode_flag,
           pps->second_chroma_qp_index_offset);
}

static void on_sei(void *opaque, const avc_sei_event_t *sei)
{
    const probe_options_t *options = (const probe_options_t *)opaque;
    unsigned i;

    if (sei->payload_type == AVC_SEI_RECOVERY_POINT && sei->parsed) {
        printf("{\"event\":\"sei\",\"nal_index\":%u,\"type\":%u,\"type_name\":\"recovery_point\",\"payload_size\":%zu,\"recovery_frame_cnt\":%u,\"exact_match\":%u,\"broken_link\":%u,\"changing_slice_group_idc\":%u}\n",
               options->current_nal_index, sei->payload_type, sei->payload_size,
               sei->recovery_point.recovery_frame_cnt,
               sei->recovery_point.exact_match_flag,
               sei->recovery_point.broken_link_flag,
               sei->recovery_point.changing_slice_group_idc);
    } else if (sei->payload_type == AVC_SEI_BUFFERING_PERIOD && sei->parsed) {
        printf("{\"event\":\"sei\",\"nal_index\":%u,\"type\":%u,\"type_name\":\"buffering_period\",\"payload_size\":%zu,\"sps_id\":%u,\"nal_initial_cpb_removal_delay0\":%u,\"nal_initial_cpb_removal_delay_offset0\":%u,\"vcl_initial_cpb_removal_delay0\":%u,\"vcl_initial_cpb_removal_delay_offset0\":%u}\n",
               options->current_nal_index, sei->payload_type, sei->payload_size,
               sei->buffering_period.seq_parameter_set_id,
               sei->buffering_period.nal_initial_cpb_removal_delay[0],
               sei->buffering_period.nal_initial_cpb_removal_delay_offset[0],
               sei->buffering_period.vcl_initial_cpb_removal_delay[0],
               sei->buffering_period.vcl_initial_cpb_removal_delay_offset[0]);
    } else if (sei->payload_type == AVC_SEI_PIC_TIMING && sei->parsed) {
        printf("{\"event\":\"sei\",\"nal_index\":%u,\"type\":%u,\"type_name\":\"pic_timing\",\"payload_size\":%zu,\"cpb_removal_delay_present\":%u,\"cpb_removal_delay\":%u,\"dpb_output_delay_present\":%u,\"dpb_output_delay\":%u,\"pic_struct_present\":%u,\"pic_struct\":%u,\"clock_timestamp_count\":%u,\"clock_timestamps\":[",
               options->current_nal_index, sei->payload_type, sei->payload_size,
               sei->pic_timing.cpb_removal_delay_present,
               sei->pic_timing.cpb_removal_delay,
               sei->pic_timing.dpb_output_delay_present,
               sei->pic_timing.dpb_output_delay,
               sei->pic_timing.pic_struct_present,
               sei->pic_timing.pic_struct,
               sei->pic_timing.clock_timestamp_count);
        for (i = 0; i < sei->pic_timing.clock_timestamp_count; i++) {
            const avc_sei_clock_timestamp_t *ts = &sei->pic_timing.clock_timestamp[i];
            printf("%s{\"present\":%u", i == 0 ? "" : ",", ts->present);
            if (ts->present) {
                printf(",\"ct_type\":%u,\"nuit_field_based\":%u,\"counting_type\":%u,"
                       "\"full_timestamp\":%u,\"discontinuity\":%u,\"cnt_dropped\":%u,"
                       "\"n_frames\":%u,\"seconds_flag\":%u,\"seconds\":%u,"
                       "\"minutes_flag\":%u,\"minutes\":%u,\"hours_flag\":%u,"
                       "\"hours\":%u,\"time_offset\":%d",
                       ts->ct_type, ts->nuit_field_based_flag, ts->counting_type,
                       ts->full_timestamp_flag, ts->discontinuity_flag,
                       ts->cnt_dropped_flag, ts->n_frames, ts->seconds_flag,
                       ts->seconds_value, ts->minutes_flag, ts->minutes_value,
                       ts->hours_flag, ts->hours_value, ts->time_offset);
            }
            printf("}");
        }
        printf("]}\n");
    } else {
        printf("{\"event\":\"sei\",\"nal_index\":%u,\"type\":%u,\"payload_size\":%zu,\"parsed\":%s}\n",
               options->current_nal_index, sei->payload_type, sei->payload_size, sei->parsed ? "true" : "false");
    }
}

static void on_slice(void *opaque, const avc_slice_header_t *slice)
{
    probe_options_t *options = (probe_options_t *)opaque;
    probe_picture_key_t key = picture_key_from_slice(slice);

    if (starts_new_picture(&options->previous_picture, &key)) {
        options->current_picture_index = options->picture_count++;
        options->slices_in_current_picture = 0;
        printf("{\"event\":\"picture\",\"picture_index\":%u,\"nal_index\":%u,\"offset\":%zu,\"idr\":%s,\"frame_num\":%u,\"poc_lsb\":%u,\"field_pic\":%u,\"bottom_field\":%u,\"nal_ref_idc\":%u}\n",
               options->current_picture_index, options->current_nal_index,
               options->current_nal_offset,
               slice->nal.nal_unit_type == AVC_NAL_SLICE_IDR ? "true" : "false",
               slice->frame_num, slice->pic_order_cnt_lsb, slice->field_pic_flag,
               slice->bottom_field_flag, slice->nal.nal_ref_idc);
    }
    options->previous_picture = key;
    options->current_slice_index = options->slice_count++;
    options->current_slice_in_picture = options->slices_in_current_picture++;
    options->have_ref_lists = 0;
    if (!want_current_slice(options)) {
        return;
    }
    printf("{\"event\":\"slice\",\"picture_index\":%u,\"slice_index\":%u,\"slice_in_picture\":%u,\"nal_index\":%u,\"nal_type\":%u,\"idr\":%s,\"slice_type\":\"%s\",\"first_mb\":%u,\"pps_id\":%u,\"frame_num\":%u,\"poc_lsb\":%u,\"field_pic\":%u,\"bottom_field\":%u,\"nal_ref_idc\":%u,\"header_bits\":%zu}\n",
           options->current_picture_index, options->current_slice_index,
           options->current_slice_in_picture, options->current_nal_index,
           slice->nal.nal_unit_type,
           slice->nal.nal_unit_type == AVC_NAL_SLICE_IDR ? "true" : "false",
           avc_slice_kind_name(slice->slice_kind),
           slice->first_mb_in_slice, slice->pic_parameter_set_id,
           slice->frame_num, slice->pic_order_cnt_lsb, slice->field_pic_flag,
           slice->bottom_field_flag, slice->nal.nal_ref_idc, slice->header_bits);
}

static void print_ref_pic_json(const avc_dpb_picture_t *pic)
{
    printf("{\"frame_num\":%u,\"poc\":%d,\"long_term\":%s,\"long_term_idx\":%u,\"idr\":%s}",
           pic->frame_num, pic->poc, pic->is_long_term ? "true" : "false",
           pic->long_term_frame_idx, pic->is_idr ? "true" : "false");
}

static void print_ref_pic_or_null(const avc_dpb_picture_t *list,
                                  unsigned count,
                                  unsigned ref_idx)
{
    if (ref_idx >= count) {
        printf("null");
        return;
    }
    print_ref_pic_json(&list[ref_idx]);
}

static void on_ref_lists(void *opaque, const avc_slice_header_t *slice,
                         const avc_ref_list_state_t *lists)
{
    unsigned i;
    probe_options_t *options = (probe_options_t *)opaque;

    options->ref_lists = *lists;
    options->have_ref_lists = 1;
    if (!want_current_slice(options)) {
        return;
    }
    printf("{\"event\":\"ref_lists\",\"picture_index\":%u,\"slice_index\":%u,\"slice_in_picture\":%u,\"nal_index\":%u,\"slice_first_mb\":%u,\"list0\":[",
           options->current_picture_index, options->current_slice_index,
           options->current_slice_in_picture, options->current_nal_index,
           slice->first_mb_in_slice);
    for (i = 0; i < lists->count_l0; i++) {
        if (i != 0) {
            printf(",");
        }
        print_ref_pic_json(&lists->l0[i]);
    }
    printf("],\"list1\":[");
    for (i = 0; i < lists->count_l1; i++) {
        if (i != 0) {
            printf(",");
        }
        print_ref_pic_json(&lists->l1[i]);
    }
    printf("]}\n");
}

static void on_macroblock(void *opaque, const avc_macroblock_event_t *mb)
{
    const probe_options_t *options = (const probe_options_t *)opaque;

    if (!want_mb(options, mb->address)) {
        return;
    }
    printf("{\"event\":\"macroblock\",\"picture_index\":%u,\"slice_index\":%u,\"slice_in_picture\":%u,\"nal_index\":%u,\"address\":%u,\"entropy\":\"%s\",\"skipped\":%s,\"mb_field_decoding_flag\":%s,\"mb_type\":%u,\"mb_skip_run\":%u,\"cbp_luma\":%u,\"cbp_chroma\":%u,\"transform_8x8\":%s,\"mb_qp_delta\":%d,\"qp_y\":%d,\"qp_cb\":%d,\"qp_cr\":%d,\"pcm_luma_samples\":%u,\"pcm_chroma_samples\":%u,\"pcm_luma_bits\":%d,\"pcm_chroma_bits\":%d}\n",
           options->current_picture_index, options->current_slice_index,
           options->current_slice_in_picture, options->current_nal_index,
           mb->address, mb->entropy == AVC_MB_ENTROPY_CABAC ? "cabac" : "cavlc",
           mb->skipped ? "true" : "false",
           mb->mb_field_decoding_flag ? "true" : "false",
           mb->mb_type, mb->mb_skip_run,
           mb->coded_block_pattern_luma, mb->coded_block_pattern_chroma,
           mb->transform_size_8x8_flag ? "true" : "false", mb->mb_qp_delta,
           mb->qp_y, mb->qp_cb, mb->qp_cr,
           mb->pcm_luma_samples, mb->pcm_chroma_samples,
           mb->pcm_sample_bits_luma, mb->pcm_sample_bits_chroma);
}

static const char *mb_pred_kind_name(avc_mb_pred_kind_t kind)
{
    switch (kind) {
    case AVC_MB_PRED_INTRA_4X4: return "intra_4x4";
    case AVC_MB_PRED_INTRA_8X8: return "intra_8x8";
    case AVC_MB_PRED_INTRA_16X16: return "intra_16x16";
    case AVC_MB_PRED_INTER: return "inter";
    case AVC_MB_PRED_PCM: return "pcm";
    default: return "unknown";
    }
}

static void print_mv_pair(const int16_t mv[2])
{
    printf("[%d,%d]", mv[0], mv[1]);
}

static void print_partition_ref_json(const probe_options_t *options,
                                     unsigned list_id,
                                     unsigned ref_idx)
{
    if (!options->have_ref_lists) {
        printf("null");
        return;
    }
    if (list_id == 0) {
        print_ref_pic_or_null(options->ref_lists.l0,
                              options->ref_lists.count_l0,
                              ref_idx);
    } else {
        print_ref_pic_or_null(options->ref_lists.l1,
                              options->ref_lists.count_l1,
                              ref_idx);
    }
}

static void print_sub_partitions(const avc_mb_pred_event_t *pred, unsigned partition)
{
    unsigned sub_count = pred->sub_partition_count[partition];
    unsigned sub_width = pred->sub_partition_width[partition];
    unsigned sub_height = pred->sub_partition_height[partition];
    unsigned i;

    if (sub_count == 0) {
        sub_count = 1;
        sub_width = pred->partition_width[partition];
        sub_height = pred->partition_height[partition];
    }
    printf("[");
    for (i = 0; i < sub_count; i++) {
        unsigned col_count = sub_width ? pred->partition_width[partition] / sub_width : 1u;
        unsigned x;
        unsigned y;

        if (i != 0) {
            printf(",");
        }
        if (col_count == 0) {
            col_count = 1;
        }
        x = pred->partition_x[partition] + (i % col_count) * sub_width;
        y = pred->partition_y[partition] + (i / col_count) * sub_height;
        printf("{\"index\":%u,\"x\":%u,\"y\":%u,\"w\":%u,\"h\":%u}",
               i, x, y, sub_width, sub_height);
    }
    printf("]");
}

static void print_partition_details(const probe_options_t *options,
                                    const avc_mb_pred_event_t *pred)
{
    unsigned i;

    printf("[");
    for (i = 0; i < pred->partition_count && i < 4; i++) {
        if (i != 0) {
            printf(",");
        }
        printf("{\"index\":%u,\"x\":%u,\"y\":%u,\"w\":%u,\"h\":%u,"
               "\"sub_mb_type\":%u,\"sub_partitions\":",
               i, pred->partition_x[i], pred->partition_y[i],
               pred->partition_width[i], pred->partition_height[i],
               pred->sub_mb_type[i]);
        print_sub_partitions(pred, i);
        printf(",\"direct\":%s,\"list_mask\":%u",
               pred->direct_flag[i] ? "true" : "false", pred->list_mask[i]);
        if (pred->list_mask[i] & 1u) {
            printf(",\"l0\":{\"ref_idx\":%u,\"ref\":", pred->ref_idx_l0[i]);
            print_partition_ref_json(options, 0, pred->ref_idx_l0[i]);
            printf(",\"mvd\":");
            print_mv_pair(pred->mvd_l0[i]);
            printf(",\"mvp\":");
            print_mv_pair(pred->mv_pred_l0[i]);
            printf(",\"mv\":");
            print_mv_pair(pred->mv_l0[i]);
            printf("}");
        }
        if (pred->list_mask[i] & 2u) {
            printf(",\"l1\":{\"ref_idx\":%u,\"ref\":", pred->ref_idx_l1[i]);
            print_partition_ref_json(options, 1, pred->ref_idx_l1[i]);
            printf(",\"mvd\":");
            print_mv_pair(pred->mvd_l1[i]);
            printf(",\"mvp\":");
            print_mv_pair(pred->mv_pred_l1[i]);
            printf(",\"mv\":");
            print_mv_pair(pred->mv_l1[i]);
            printf("}");
        }
        printf("}");
    }
    printf("]");
}

static void print_sub_partition_motion_details(const probe_options_t *options,
                                               const avc_mb_pred_event_t *pred)
{
    unsigned i;

    printf("[");
    for (i = 0;
         i < pred->sub_partition_total && i < AVC_MB_PRED_MAX_SUB_PARTITIONS;
         i++) {
        if (i != 0) {
            printf(",");
        }
        printf("{\"index\":%u,\"parent\":%u,\"x\":%u,\"y\":%u,\"w\":%u,\"h\":%u,"
               "\"direct\":%s,\"list_mask\":%u",
               i, pred->sub_partition_parent[i], pred->sub_part_x[i],
               pred->sub_part_y[i], pred->sub_part_width[i],
               pred->sub_part_height[i],
               pred->sub_part_direct_flag[i] ? "true" : "false",
               pred->sub_part_list_mask[i]);
        if (pred->sub_part_list_mask[i] & 1u) {
            printf(",\"l0\":{\"ref_idx\":%u,\"ref\":",
                   pred->sub_part_ref_idx_l0[i]);
            print_partition_ref_json(options, 0, pred->sub_part_ref_idx_l0[i]);
            printf(",\"mvd\":");
            print_mv_pair(pred->sub_part_mvd_l0[i]);
            printf(",\"mvp\":");
            print_mv_pair(pred->sub_part_mv_pred_l0[i]);
            printf(",\"mv\":");
            print_mv_pair(pred->sub_part_mv_l0[i]);
            printf("}");
        }
        if (pred->sub_part_list_mask[i] & 2u) {
            printf(",\"l1\":{\"ref_idx\":%u,\"ref\":",
                   pred->sub_part_ref_idx_l1[i]);
            print_partition_ref_json(options, 1, pred->sub_part_ref_idx_l1[i]);
            printf(",\"mvd\":");
            print_mv_pair(pred->sub_part_mvd_l1[i]);
            printf(",\"mvp\":");
            print_mv_pair(pred->sub_part_mv_pred_l1[i]);
            printf(",\"mv\":");
            print_mv_pair(pred->sub_part_mv_l1[i]);
            printf("}");
        }
        printf("}");
    }
    printf("]");
}

static void on_mb_pred(void *opaque, const avc_mb_pred_event_t *pred)
{
    const probe_options_t *options = (const probe_options_t *)opaque;

    if (!want_mb(options, pred->mb_address)) {
        return;
    }
    printf("{\"event\":\"mb_pred\",\"picture_index\":%u,\"slice_index\":%u,\"slice_in_picture\":%u,\"nal_index\":%u,\"mb_address\":%u,\"kind\":\"%s\",\"intra_chroma_pred_mode\":%u,\"partitions\":%u,\"direct_spatial\":%u,\"direct\":[%u,%u,%u,%u],\"list_mask\":[%u,%u,%u,%u],\"ref0\":[%u,%u,%u,%u],\"ref1\":[%u,%u,%u,%u],\"mvd0\":[[%d,%d],[%d,%d],[%d,%d],[%d,%d]],\"mvd1\":[[%d,%d],[%d,%d],[%d,%d],[%d,%d]],\"mv_pred0\":[[%d,%d],[%d,%d],[%d,%d],[%d,%d]],\"mv_pred1\":[[%d,%d],[%d,%d],[%d,%d],[%d,%d]],\"mv0\":[[%d,%d],[%d,%d],[%d,%d],[%d,%d]],\"mv1\":[[%d,%d],[%d,%d],[%d,%d],[%d,%d]],\"sub_mb_type\":[%u,%u,%u,%u],\"sub_partition_total\":%u,\"partition_detail\":",
           options->current_picture_index, options->current_slice_index,
           options->current_slice_in_picture, options->current_nal_index,
           pred->mb_address, mb_pred_kind_name(pred->kind), pred->intra_chroma_pred_mode,
           pred->partition_count,
           pred->direct_spatial_mv_pred_flag,
           pred->direct_flag[0], pred->direct_flag[1], pred->direct_flag[2], pred->direct_flag[3],
           pred->list_mask[0], pred->list_mask[1], pred->list_mask[2], pred->list_mask[3],
           pred->ref_idx_l0[0], pred->ref_idx_l0[1], pred->ref_idx_l0[2], pred->ref_idx_l0[3],
           pred->ref_idx_l1[0], pred->ref_idx_l1[1], pred->ref_idx_l1[2], pred->ref_idx_l1[3],
           pred->mvd_l0[0][0], pred->mvd_l0[0][1],
           pred->mvd_l0[1][0], pred->mvd_l0[1][1],
           pred->mvd_l0[2][0], pred->mvd_l0[2][1],
           pred->mvd_l0[3][0], pred->mvd_l0[3][1],
           pred->mvd_l1[0][0], pred->mvd_l1[0][1],
           pred->mvd_l1[1][0], pred->mvd_l1[1][1],
           pred->mvd_l1[2][0], pred->mvd_l1[2][1],
           pred->mvd_l1[3][0], pred->mvd_l1[3][1],
           pred->mv_pred_l0[0][0], pred->mv_pred_l0[0][1],
           pred->mv_pred_l0[1][0], pred->mv_pred_l0[1][1],
           pred->mv_pred_l0[2][0], pred->mv_pred_l0[2][1],
           pred->mv_pred_l0[3][0], pred->mv_pred_l0[3][1],
           pred->mv_pred_l1[0][0], pred->mv_pred_l1[0][1],
           pred->mv_pred_l1[1][0], pred->mv_pred_l1[1][1],
           pred->mv_pred_l1[2][0], pred->mv_pred_l1[2][1],
           pred->mv_pred_l1[3][0], pred->mv_pred_l1[3][1],
           pred->mv_l0[0][0], pred->mv_l0[0][1],
           pred->mv_l0[1][0], pred->mv_l0[1][1],
           pred->mv_l0[2][0], pred->mv_l0[2][1],
           pred->mv_l0[3][0], pred->mv_l0[3][1],
           pred->mv_l1[0][0], pred->mv_l1[0][1],
           pred->mv_l1[1][0], pred->mv_l1[1][1],
           pred->mv_l1[2][0], pred->mv_l1[2][1],
           pred->mv_l1[3][0], pred->mv_l1[3][1],
           pred->sub_mb_type[0], pred->sub_mb_type[1],
           pred->sub_mb_type[2], pred->sub_mb_type[3],
           pred->sub_partition_total);
    print_partition_details(options, pred);
    printf(",\"sub_partition_detail\":");
    print_sub_partition_motion_details(options, pred);
    printf("}\n");
}

static const char *residual_kind_name(avc_residual_kind_t kind)
{
    switch (kind) {
    case AVC_RESIDUAL_LUMA_4X4: return "luma_4x4";
    case AVC_RESIDUAL_LUMA_8X8: return "luma_8x8";
    case AVC_RESIDUAL_LUMA_16X16_DC: return "luma_16x16_dc";
    case AVC_RESIDUAL_LUMA_16X16_AC: return "luma_16x16_ac";
    case AVC_RESIDUAL_CHROMA_DC: return "chroma_dc";
    case AVC_RESIDUAL_CHROMA_AC: return "chroma_ac";
    default: return "unknown";
    }
}

static void on_residual(void *opaque, const avc_residual_event_t *residual)
{
    const probe_options_t *options = (const probe_options_t *)opaque;
    unsigned i;
    unsigned emitted = 0;

    if (!want_mb(options, residual->mb_address)) {
        return;
    }
    if (residual->entropy == AVC_MB_ENTROPY_CABAC) {
        printf("{\"event\":\"residual\",\"picture_index\":%u,\"slice_index\":%u,\"slice_in_picture\":%u,\"nal_index\":%u,\"entropy\":\"cabac\",\"mb_address\":%u,\"block_kind\":\"%s\",\"block_index\":%u,\"component\":%u,\"chroma_format_idc\":%u,\"chroma_array_type\":%u,\"separate_colour_plane\":%u,\"bit_depth_luma\":%u,\"bit_depth_chroma\":%u,\"qp_y\":%d,\"qp_cb\":%d,\"qp_cr\":%d,\"qp_for_block\":%d,\"transform_bypass\":%u,\"scaling_list_size\":%u,\"scaling_list_index\":%d,\"scaling_list_present\":%u,\"scaling_list_default\":%u,\"total_coeff\":%u,\"coefficients\":[",
               options->current_picture_index, options->current_slice_index,
               options->current_slice_in_picture, options->current_nal_index,
               residual->mb_address, residual_kind_name(residual->block_kind), residual->block_index,
               residual->component, residual->chroma_format_idc, residual->chroma_array_type,
               residual->separate_colour_plane_flag, residual->bit_depth_luma, residual->bit_depth_chroma,
               residual->qp_y, residual->qp_cb, residual->qp_cr, residual->qp_for_block,
               residual->transform_bypass, residual->scaling_list_size, residual->scaling_list_index,
               residual->scaling_list_present_flag, residual->scaling_list_use_default_flag,
               residual->cabac_block.total_coeff);
        for (i = 0; i < residual->cabac_block.max_coeff; i++) {
            if (residual->cabac_block.coeff_level[i] != 0) {
                printf("%s{\"scan\":%u,\"x\":%u,\"y\":%u,\"mb_x\":%u,\"mb_y\":%u,\"level\":%d}",
                       emitted == 0 ? "" : ",", i,
                       residual->cabac_block.coeff_x[i], residual->cabac_block.coeff_y[i],
                       residual->coeff_mb_x[i], residual->coeff_mb_y[i],
                       residual->cabac_block.coeff_level[i]);
                emitted++;
            }
        }
        printf("]}\n");
    } else {
        printf("{\"event\":\"residual\",\"picture_index\":%u,\"slice_index\":%u,\"slice_in_picture\":%u,\"nal_index\":%u,\"entropy\":\"cavlc\",\"mb_address\":%u,\"block_kind\":\"%s\",\"block_index\":%u,\"component\":%u,\"chroma_format_idc\":%u,\"chroma_array_type\":%u,\"separate_colour_plane\":%u,\"bit_depth_luma\":%u,\"bit_depth_chroma\":%u,\"qp_y\":%d,\"qp_cb\":%d,\"qp_cr\":%d,\"qp_for_block\":%d,\"transform_bypass\":%u,\"scaling_list_size\":%u,\"scaling_list_index\":%d,\"scaling_list_present\":%u,\"scaling_list_default\":%u,\"total_coeff\":%u,\"trailing_ones\":%u,\"total_zeros\":%u,\"coefficients\":[",
               options->current_picture_index, options->current_slice_index,
               options->current_slice_in_picture, options->current_nal_index,
               residual->mb_address, residual_kind_name(residual->block_kind), residual->block_index,
               residual->component, residual->chroma_format_idc, residual->chroma_array_type,
               residual->separate_colour_plane_flag, residual->bit_depth_luma, residual->bit_depth_chroma,
               residual->qp_y, residual->qp_cb, residual->qp_cr, residual->qp_for_block,
               residual->transform_bypass, residual->scaling_list_size, residual->scaling_list_index,
               residual->scaling_list_present_flag, residual->scaling_list_use_default_flag,
               residual->block.total_coeff, residual->block.trailing_ones,
               residual->block.total_zeros);
        for (i = 0; i < residual->block.total_coeff; i++) {
            printf("%s{\"coded_index\":%u,\"scan\":%u,\"x\":%u,\"y\":%u,\"mb_x\":%u,\"mb_y\":%u,\"level\":%d,\"run_before\":%u}",
                   i == 0 ? "" : ",", i, residual->block.coeff_scan[i],
                   residual->block.coeff_x[i], residual->block.coeff_y[i],
                   residual->coeff_mb_x[i], residual->coeff_mb_y[i],
                   residual->block.coeff_level[i], residual->block.run_before[i]);
        }
        printf("]}\n");
    }
}

static void on_slice_data(void *opaque, const avc_slice_data_summary_t *summary)
{
    const probe_options_t *options = (const probe_options_t *)opaque;

    if (!want_current_slice(options)) {
        return;
    }
    printf("{\"event\":\"slice_data\",\"picture_index\":%u,\"slice_index\":%u,\"slice_in_picture\":%u,\"nal_index\":%u,\"entropy\":\"%s\",\"macroblocks_seen\":%u,\"max_macroblocks\":%u,\"cabac_initialized\":%s,\"complete\":%s}\n",
           options->current_picture_index, options->current_slice_index,
           options->current_slice_in_picture, options->current_nal_index,
           summary->entropy_coding_mode_flag ? "cabac" : "cavlc",
           summary->macroblocks_seen, summary->max_macroblocks,
           summary->cabac_initialized ? "true" : "false",
           summary->complete ? "true" : "false");
}

static void on_note(void *opaque, const char *message)
{
    const probe_options_t *options = (const probe_options_t *)opaque;

    fprintf(stderr, "{\"event\":\"note\",\"picture_index\":%u,\"slice_index\":%u,\"nal_index\":%u,\"message\":\"%s\"}\n",
            options->current_picture_index, options->current_slice_index,
            options->current_nal_index, message);
}

static void on_error(void *opaque, const char *message, size_t offset)
{
    const probe_options_t *options = (const probe_options_t *)opaque;

    fprintf(stderr, "{\"event\":\"error\",\"picture_index\":%u,\"slice_index\":%u,\"nal_index\":%u,\"offset\":%zu,\"message\":\"%s\"}\n",
            options->current_picture_index, options->current_slice_index,
            options->current_nal_index, offset, message);
}

static uint8_t *read_file(const char *path, size_t *size_out)
{
    FILE *f = fopen(path, "rb");
    uint8_t *data;
    long size;

    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    data = (uint8_t *)malloc((size_t)size == 0 ? 1 : (size_t)size);
    if (!data) {
        fclose(f);
        return NULL;
    }
    if (fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size_out = (size_t)size;
    return data;
}

static int parse_u32_arg(const char *text, const char *label, uint32_t *out)
{
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);

    if (!end || *end != '\0' || value > 0xffffffffUL) {
        fprintf(stderr, "invalid %s: %s\n", label, text);
        return 0;
    }
    *out = (uint32_t)value;
    return 1;
}

static void usage(const char *progname)
{
    fprintf(stderr, "usage: %s [-p index|--picture index] [-s index|--slice index] [--slice-in-picture index] [-m address|--mb address] [-i filename|--input filename] input.264\n",
            progname);
}

int main(int argc, char **argv)
{
    avc_parser_t parser;
    avc_parser_callbacks_t callbacks = {0};
    probe_options_t options = {0};
    uint8_t *data;
    size_t size = 0;
    int ok;
    int opt;
    enum {
        OPT_SLICE_IN_PICTURE = 1000
    };
    static const struct option long_options[] = {
        {"mb", required_argument, NULL, 'm'},
        {"picture", required_argument, NULL, 'p'},
        {"slice", required_argument, NULL, 's'},
        {"slice-in-picture", required_argument, NULL, OPT_SLICE_IN_PICTURE},
        {"input", required_argument, NULL, 'i'},
        {NULL, 0, NULL, 0}
    };

    while ((opt = getopt_long(argc, argv, "m:p:s:i:", long_options, NULL)) != -1) {
        switch (opt) {
        case 'm':
            if (!parse_u32_arg(optarg, "macroblock address", &options.mb_address)) {
                return 2;
            }
            options.filter_mb = 1;
            break;
        case 'p':
            if (!parse_u32_arg(optarg, "picture index", &options.picture_index)) {
                return 2;
            }
            options.filter_picture = 1;
            break;
        case 's':
            if (!parse_u32_arg(optarg, "slice index", &options.slice_index)) {
                return 2;
            }
            options.filter_slice = 1;
            break;
        case OPT_SLICE_IN_PICTURE:
            if (!parse_u32_arg(optarg, "slice-in-picture index", &options.slice_in_picture)) {
                return 2;
            }
            options.filter_slice_in_picture = 1;
            break;
        case 'i':
            options.input = strdup(optarg);
            break;
        default:
            usage(argv[0]);
            return 2;
        }
    }
    if (!options.input) {
        if (optind + 1 != argc) {
            usage(argv[0]);
            return 2;
        }
        options.input = argv[optind];
    }

    data = read_file(options.input, &size);
    if (!data) {
        fprintf(stderr, "failed to read %s\n", options.input);
        return 1;
    }

    callbacks.on_nal = on_nal;
    callbacks.on_sps = on_sps;
    callbacks.on_pps = on_pps;
    callbacks.on_sei = on_sei;
    callbacks.on_slice = on_slice;
    callbacks.on_ref_lists = on_ref_lists;
    callbacks.on_macroblock = on_macroblock;
    callbacks.on_mb_pred = on_mb_pred;
    callbacks.on_residual = on_residual;
    callbacks.on_slice_data = on_slice_data;
    callbacks.on_note = on_note;
    callbacks.on_error = on_error;
    avc_parser_init(&parser, callbacks, &options);
    ok = avc_parser_parse_annexb(&parser, data, size);
    free(data);
    return ok ? 0 : 1;
}
