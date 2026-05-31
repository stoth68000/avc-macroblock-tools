#include "avc/avc_syntax.h"
#include "avc/avc_bitreader.h"

static int is_high_profile(uint32_t profile_idc)
{
    switch (profile_idc) {
    case 100: case 110: case 122: case 244: case 44:
    case 83: case 86: case 118: case 128: case 138:
    case 139: case 134: case 135:
        return 1;
    default:
        return 0;
    }
}

static unsigned sps_chroma_array_type(const avc_sps_t *sps)
{
    if (sps->separate_colour_plane_flag) {
        return 0;
    }
    return sps->chroma_format_idc;
}

static const uint8_t default_4x4_intra[16] = {
    6, 13, 20, 28,
    13, 20, 28, 32,
    20, 28, 32, 37,
    28, 32, 37, 42
};

static const uint8_t default_4x4_inter[16] = {
    10, 14, 20, 24,
    14, 20, 24, 27,
    20, 24, 27, 30,
    24, 27, 30, 34
};

static const uint8_t default_8x8_intra[64] = {
    6, 10, 13, 16, 18, 23, 25, 27,
    10, 11, 16, 18, 23, 25, 27, 29,
    13, 16, 18, 23, 25, 27, 29, 31,
    16, 18, 23, 25, 27, 29, 31, 33,
    18, 23, 25, 27, 29, 31, 33, 36,
    23, 25, 27, 29, 31, 33, 36, 38,
    25, 27, 29, 31, 33, 36, 38, 40,
    27, 29, 31, 33, 36, 38, 40, 42
};

static const uint8_t default_8x8_inter[64] = {
    9, 13, 15, 17, 19, 21, 22, 24,
    13, 13, 17, 19, 21, 22, 24, 25,
    15, 17, 19, 21, 22, 24, 25, 27,
    17, 19, 21, 22, 24, 25, 27, 28,
    19, 21, 22, 24, 25, 27, 28, 30,
    21, 22, 24, 25, 27, 28, 30, 32,
    22, 24, 25, 27, 28, 30, 32, 33,
    24, 25, 27, 28, 30, 32, 33, 35
};

static void fill_flat_scaling_lists(avc_sps_t *sps)
{
    uint32_t i;
    uint32_t j;

    for (i = 0; i < AVC_SPS_SCALING_LIST_4X4_COUNT; i++) {
        for (j = 0; j < 16; j++) {
            sps->scaling_list_4x4[i][j] = 16;
        }
    }
    for (i = 0; i < AVC_SPS_SCALING_LIST_8X8_COUNT; i++) {
        for (j = 0; j < 64; j++) {
            sps->scaling_list_8x8[i][j] = 16;
        }
    }
}

static void fill_flat_pps_scaling_lists(avc_pps_t *pps)
{
    uint32_t i;
    uint32_t j;

    for (i = 0; i < AVC_SPS_SCALING_LIST_4X4_COUNT; i++) {
        for (j = 0; j < 16; j++) {
            pps->scaling_list_4x4[i][j] = 16;
        }
    }
    for (i = 0; i < AVC_SPS_SCALING_LIST_8X8_COUNT; i++) {
        for (j = 0; j < 64; j++) {
            pps->scaling_list_8x8[i][j] = 16;
        }
    }
}

static void copy_scaling_list(uint8_t *dst, const uint8_t *src, uint32_t size)
{
    uint32_t i;

    for (i = 0; i < size; i++) {
        dst[i] = src[i];
    }
}

static const uint8_t *default_4x4_for_index(uint32_t index)
{
    return index < 3 ? default_4x4_intra : default_4x4_inter;
}

static const uint8_t *default_8x8_for_index(uint32_t index)
{
    return (index & 1u) == 0 ? default_8x8_intra : default_8x8_inter;
}

static void fallback_scaling_list_4x4(avc_sps_t *sps, uint32_t index)
{
    if (index == 0 || index == 3) {
        copy_scaling_list(sps->scaling_list_4x4[index],
                          default_4x4_for_index(index), 16);
    } else {
        copy_scaling_list(sps->scaling_list_4x4[index],
                          sps->scaling_list_4x4[index - 1u], 16);
    }
}

static void fallback_scaling_list_8x8(avc_sps_t *sps, uint32_t index)
{
    if (index == 0 || index == 1) {
        copy_scaling_list(sps->scaling_list_8x8[index],
                          default_8x8_for_index(index), 64);
    } else {
        copy_scaling_list(sps->scaling_list_8x8[index],
                          sps->scaling_list_8x8[index - 1u], 64);
    }
}

static void fallback_pps_scaling_list_4x4(avc_pps_t *pps, uint32_t index)
{
    if (index == 0 || index == 3) {
        copy_scaling_list(pps->scaling_list_4x4[index],
                          default_4x4_for_index(index), 16);
    } else {
        copy_scaling_list(pps->scaling_list_4x4[index],
                          pps->scaling_list_4x4[index - 1u], 16);
    }
}

static void fallback_pps_scaling_list_8x8(avc_pps_t *pps, uint32_t index)
{
    if (index == 0 || index == 1) {
        copy_scaling_list(pps->scaling_list_8x8[index],
                          default_8x8_for_index(index), 64);
    } else {
        copy_scaling_list(pps->scaling_list_8x8[index],
                          pps->scaling_list_8x8[index - 1u], 64);
    }
}

static int parse_scaling_list(avc_bitreader_t *br,
                              uint8_t *dst,
                              uint32_t size,
                              const uint8_t *default_list,
                              uint8_t *use_default)
{
    int last_scale = 8;
    int next_scale = 8;
    uint32_t j;

    *use_default = 0;
    for (j = 0; j < size; j++) {
        if (next_scale != 0) {
            int delta_scale = avc_br_read_se(br);
            next_scale = (last_scale + delta_scale + 256) % 256;
            if (j == 0 && next_scale == 0) {
                *use_default = 1;
                copy_scaling_list(dst, default_list, size);
            }
        }
        if (*use_default) {
            last_scale = default_list[j];
        } else {
            dst[j] = (uint8_t)(next_scale == 0 ? last_scale : next_scale);
            last_scale = dst[j];
        }
    }
    return !br->error;
}

static int parse_sps_scaling_matrices(avc_bitreader_t *br,
                                      avc_sps_t *sps,
                                      uint32_t count)
{
    uint32_t i;

    sps->seq_scaling_matrix_present_flag = (uint8_t)avc_br_read_bit(br);
    if (!sps->seq_scaling_matrix_present_flag) {
        return !br->error;
    }
    for (i = 0; i < count; i++) {
        sps->seq_scaling_list_present_flag[i] = (uint8_t)avc_br_read_bit(br);
        if (br->error) {
            return 0;
        }
        if (i < 6) {
            if (sps->seq_scaling_list_present_flag[i]) {
                if (!parse_scaling_list(br, sps->scaling_list_4x4[i], 16,
                                        default_4x4_for_index(i),
                                        &sps->use_default_scaling_matrix_flag[i])) {
                    return 0;
                }
            } else {
                fallback_scaling_list_4x4(sps, i);
            }
        } else {
            uint32_t index = i - 6u;
            if (sps->seq_scaling_list_present_flag[i]) {
                if (!parse_scaling_list(br, sps->scaling_list_8x8[index], 64,
                                        default_8x8_for_index(index),
                                        &sps->use_default_scaling_matrix_flag[i])) {
                    return 0;
                }
            } else {
                fallback_scaling_list_8x8(sps, index);
            }
        }
    }
    return 1;
}

static int parse_pps_scaling_matrices(avc_bitreader_t *br,
                                      avc_pps_t *pps,
                                      uint32_t count)
{
    uint32_t i;

    pps->pic_scaling_matrix_present_flag = (uint8_t)avc_br_read_bit(br);
    if (!pps->pic_scaling_matrix_present_flag) {
        return !br->error;
    }
    for (i = 0; i < count; i++) {
        pps->pic_scaling_list_present_flag[i] = (uint8_t)avc_br_read_bit(br);
        if (br->error) {
            return 0;
        }
        if (i < 6) {
            if (pps->pic_scaling_list_present_flag[i]) {
                if (!parse_scaling_list(br, pps->scaling_list_4x4[i], 16,
                                        default_4x4_for_index(i),
                                        &pps->use_default_scaling_matrix_flag[i])) {
                    return 0;
                }
            } else {
                fallback_pps_scaling_list_4x4(pps, i);
            }
        } else {
            uint32_t index = i - 6u;
            if (pps->pic_scaling_list_present_flag[i]) {
                if (!parse_scaling_list(br, pps->scaling_list_8x8[index], 64,
                                        default_8x8_for_index(index),
                                        &pps->use_default_scaling_matrix_flag[i])) {
                    return 0;
                }
            } else {
                fallback_pps_scaling_list_8x8(pps, index);
            }
        }
    }
    return 1;
}

static unsigned bits_for_slice_group_id(uint32_t num_slice_groups_minus1)
{
    unsigned bits = 0;
    uint32_t value = num_slice_groups_minus1;

    while (value != 0) {
        bits++;
        value >>= 1;
    }
    return bits ? bits : 1;
}

static int parse_pps_slice_groups(avc_bitreader_t *br, avc_pps_t *pps)
{
    uint32_t i;

    if (pps->num_slice_groups_minus1 == 0) {
        return 1;
    }
    if (pps->num_slice_groups_minus1 >= AVC_MAX_SLICE_GROUPS) {
        br->error = 1;
        return 0;
    }

    pps->slice_group_map_type = avc_br_read_ue(br);
    if (pps->slice_group_map_type == 0) {
        for (i = 0; i <= pps->num_slice_groups_minus1; i++) {
            pps->run_length_minus1[i] = avc_br_read_ue(br);
        }
    } else if (pps->slice_group_map_type == 2) {
        for (i = 0; i < pps->num_slice_groups_minus1; i++) {
            pps->top_left[i] = avc_br_read_ue(br);
            pps->bottom_right[i] = avc_br_read_ue(br);
        }
    } else if (pps->slice_group_map_type == 3 ||
               pps->slice_group_map_type == 4 ||
               pps->slice_group_map_type == 5) {
        pps->slice_group_change_direction_flag = (uint8_t)avc_br_read_bit(br);
        pps->slice_group_change_rate_minus1 = avc_br_read_ue(br);
    } else if (pps->slice_group_map_type == 6) {
        unsigned bits;
        pps->pic_size_in_map_units_minus1 = avc_br_read_ue(br);
        if (pps->pic_size_in_map_units_minus1 == 0xffffffffu) {
            br->error = 1;
            return 0;
        }
        bits = bits_for_slice_group_id(pps->num_slice_groups_minus1);
        pps->slice_group_id_count = pps->pic_size_in_map_units_minus1 + 1u;
        if (pps->slice_group_id_count > AVC_MAX_SLICE_GROUP_ID) {
            pps->slice_group_id_truncated = 1;
        }
        for (i = 0; i < pps->slice_group_id_count; i++) {
            uint8_t id = (uint8_t)avc_br_read_bits(br, bits);
            if (id > pps->num_slice_groups_minus1) {
                br->error = 1;
                return 0;
            }
            if (i < AVC_MAX_SLICE_GROUP_ID) {
                pps->slice_group_id[i] = id;
            }
        }
    } else if (pps->slice_group_map_type > 6) {
        br->error = 1;
        return 0;
    }
    return !br->error;
}

static int parse_hrd_parameters(avc_bitreader_t *br,
                                avc_hrd_parameters_t *hrd)
{
    uint32_t i;

    *hrd = (avc_hrd_parameters_t){0};
    hrd->present = 1;
    hrd->cpb_cnt_minus1 = avc_br_read_ue(br);
    if (hrd->cpb_cnt_minus1 >= AVC_MAX_HRD_CPB_CNT) {
        br->error = 1;
        return 0;
    }
    hrd->bit_rate_scale = (uint8_t)avc_br_read_bits(br, 4);
    hrd->cpb_size_scale = (uint8_t)avc_br_read_bits(br, 4);
    for (i = 0; i <= hrd->cpb_cnt_minus1; i++) {
        hrd->bit_rate_value_minus1[i] = avc_br_read_ue(br);
        hrd->cpb_size_value_minus1[i] = avc_br_read_ue(br);
        hrd->cbr_flag[i] = (uint8_t)avc_br_read_bit(br);
    }
    hrd->initial_cpb_removal_delay_length_minus1 = (uint8_t)avc_br_read_bits(br, 5);
    hrd->cpb_removal_delay_length_minus1 = (uint8_t)avc_br_read_bits(br, 5);
    hrd->dpb_output_delay_length_minus1 = (uint8_t)avc_br_read_bits(br, 5);
    hrd->time_offset_length = (uint8_t)avc_br_read_bits(br, 5);
    return !br->error;
}

static int parse_vui_parameters(avc_bitreader_t *br,
                                avc_vui_parameters_t *vui)
{
    *vui = (avc_vui_parameters_t){0};

    vui->aspect_ratio_info_present_flag = (uint8_t)avc_br_read_bit(br);
    if (vui->aspect_ratio_info_present_flag) {
        vui->aspect_ratio_idc = (uint8_t)avc_br_read_bits(br, 8);
        if (vui->aspect_ratio_idc == 255) {
            vui->sar_width = (uint16_t)avc_br_read_bits(br, 16);
            vui->sar_height = (uint16_t)avc_br_read_bits(br, 16);
        }
    }

    vui->overscan_info_present_flag = (uint8_t)avc_br_read_bit(br);
    if (vui->overscan_info_present_flag) {
        vui->overscan_appropriate_flag = (uint8_t)avc_br_read_bit(br);
    }

    vui->video_signal_type_present_flag = (uint8_t)avc_br_read_bit(br);
    if (vui->video_signal_type_present_flag) {
        vui->video_format = (uint8_t)avc_br_read_bits(br, 3);
        vui->video_full_range_flag = (uint8_t)avc_br_read_bit(br);
        vui->colour_description_present_flag = (uint8_t)avc_br_read_bit(br);
        if (vui->colour_description_present_flag) {
            vui->colour_primaries = (uint8_t)avc_br_read_bits(br, 8);
            vui->transfer_characteristics = (uint8_t)avc_br_read_bits(br, 8);
            vui->matrix_coefficients = (uint8_t)avc_br_read_bits(br, 8);
        }
    }

    vui->chroma_loc_info_present_flag = (uint8_t)avc_br_read_bit(br);
    if (vui->chroma_loc_info_present_flag) {
        vui->chroma_sample_loc_type_top_field = avc_br_read_ue(br);
        vui->chroma_sample_loc_type_bottom_field = avc_br_read_ue(br);
    }

    vui->timing_info_present_flag = (uint8_t)avc_br_read_bit(br);
    if (vui->timing_info_present_flag) {
        vui->num_units_in_tick = avc_br_read_bits(br, 32);
        vui->time_scale = avc_br_read_bits(br, 32);
        vui->fixed_frame_rate_flag = (uint8_t)avc_br_read_bit(br);
    }

    vui->nal_hrd_parameters_present_flag = (uint8_t)avc_br_read_bit(br);
    if (vui->nal_hrd_parameters_present_flag &&
        !parse_hrd_parameters(br, &vui->nal_hrd_parameters)) {
        return 0;
    }
    vui->vcl_hrd_parameters_present_flag = (uint8_t)avc_br_read_bit(br);
    if (vui->vcl_hrd_parameters_present_flag &&
        !parse_hrd_parameters(br, &vui->vcl_hrd_parameters)) {
        return 0;
    }
    if (vui->nal_hrd_parameters_present_flag || vui->vcl_hrd_parameters_present_flag) {
        vui->low_delay_hrd_flag = (uint8_t)avc_br_read_bit(br);
    }
    vui->pic_struct_present_flag = (uint8_t)avc_br_read_bit(br);

    vui->bitstream_restriction_flag = (uint8_t)avc_br_read_bit(br);
    if (vui->bitstream_restriction_flag) {
        vui->motion_vectors_over_pic_boundaries_flag = (uint8_t)avc_br_read_bit(br);
        vui->max_bytes_per_pic_denom = avc_br_read_ue(br);
        vui->max_bits_per_mb_denom = avc_br_read_ue(br);
        vui->log2_max_mv_length_horizontal = avc_br_read_ue(br);
        vui->log2_max_mv_length_vertical = avc_br_read_ue(br);
        vui->max_num_reorder_frames = avc_br_read_ue(br);
        vui->max_dec_frame_buffering = avc_br_read_ue(br);
    }
    return !br->error;
}

int avc_parse_sps(const uint8_t *rbsp, size_t rbsp_size, avc_sps_t *sps)
{
    avc_bitreader_t br;
    uint32_t i;

    *sps = (avc_sps_t){0};
    fill_flat_scaling_lists(sps);
    avc_br_init(&br, rbsp, rbsp_size);

    sps->profile_idc = (uint8_t)avc_br_read_bits(&br, 8);
    sps->constraint_set_flags = (uint8_t)avc_br_read_bits(&br, 8);
    sps->level_idc = (uint8_t)avc_br_read_bits(&br, 8);
    sps->seq_parameter_set_id = avc_br_read_ue(&br);
    if (sps->seq_parameter_set_id >= AVC_MAX_SPS) {
        return 0;
    }

    sps->chroma_format_idc = 1;
    if (is_high_profile(sps->profile_idc)) {
        sps->chroma_format_idc = avc_br_read_ue(&br);
        if (sps->chroma_format_idc > 3) {
            return 0;
        }
        if (sps->chroma_format_idc == 3) {
            sps->separate_colour_plane_flag = (uint8_t)avc_br_read_bit(&br);
        }
        sps->bit_depth_luma_minus8 = avc_br_read_ue(&br);
        sps->bit_depth_chroma_minus8 = avc_br_read_ue(&br);
        if (sps->bit_depth_luma_minus8 > 6 || sps->bit_depth_chroma_minus8 > 6) {
            return 0;
        }
        sps->qpprime_y_zero_transform_bypass_flag = (uint8_t)avc_br_read_bit(&br);
        if (!parse_sps_scaling_matrices(&br, sps,
                                        sps->chroma_format_idc != 3 ? 8u : 12u)) {
            return 0;
        }
    }

    sps->log2_max_frame_num_minus4 = avc_br_read_ue(&br);
    if (sps->log2_max_frame_num_minus4 > 12) {
        return 0;
    }
    sps->pic_order_cnt_type = avc_br_read_ue(&br);
    if (sps->pic_order_cnt_type == 0) {
        sps->log2_max_pic_order_cnt_lsb_minus4 = avc_br_read_ue(&br);
        if (sps->log2_max_pic_order_cnt_lsb_minus4 > 12) {
            return 0;
        }
    } else if (sps->pic_order_cnt_type == 1) {
        sps->delta_pic_order_always_zero_flag = (uint8_t)avc_br_read_bit(&br);
        sps->offset_for_non_ref_pic = avc_br_read_se(&br);
        sps->offset_for_top_to_bottom_field = avc_br_read_se(&br);
        sps->num_ref_frames_in_pic_order_cnt_cycle = avc_br_read_ue(&br);
        if (sps->num_ref_frames_in_pic_order_cnt_cycle > 255) {
            return 0;
        }
        for (i = 0; i < sps->num_ref_frames_in_pic_order_cnt_cycle; i++) {
            sps->offset_for_ref_frame[i] = avc_br_read_se(&br);
        }
    } else if (sps->pic_order_cnt_type > 2) {
        return 0;
    }

    sps->max_num_ref_frames = avc_br_read_ue(&br);
    sps->gaps_in_frame_num_value_allowed_flag = (uint8_t)avc_br_read_bit(&br);
    sps->pic_width_in_mbs_minus1 = avc_br_read_ue(&br);
    sps->pic_height_in_map_units_minus1 = avc_br_read_ue(&br);
    sps->frame_mbs_only_flag = (uint8_t)avc_br_read_bit(&br);
    if (!sps->frame_mbs_only_flag) {
        sps->mb_adaptive_frame_field_flag = (uint8_t)avc_br_read_bit(&br);
    }
    sps->direct_8x8_inference_flag = (uint8_t)avc_br_read_bit(&br);
    sps->frame_cropping_flag = (uint8_t)avc_br_read_bit(&br);
    if (sps->frame_cropping_flag) {
        sps->frame_crop_left_offset = avc_br_read_ue(&br);
        sps->frame_crop_right_offset = avc_br_read_ue(&br);
        sps->frame_crop_top_offset = avc_br_read_ue(&br);
        sps->frame_crop_bottom_offset = avc_br_read_ue(&br);
    }
    sps->vui_parameters_present_flag = (uint8_t)avc_br_read_bit(&br);
    if (sps->vui_parameters_present_flag &&
        !parse_vui_parameters(&br, &sps->vui)) {
        return 0;
    }

    if (br.error) {
        return 0;
    }
    sps->present = 1;
    return 1;
}

static uint32_t pps_scaling_list_count(const avc_parameter_sets_t *sets,
                                       const avc_pps_t *pps)
{
    const avc_sps_t *sps;

    if (!pps->transform_8x8_mode_flag) {
        return 6;
    }
    if (!sets || pps->seq_parameter_set_id >= AVC_MAX_SPS) {
        return 8;
    }
    sps = &sets->sps[pps->seq_parameter_set_id];
    if (!sps->present) {
        return 8;
    }
    return 6u + (sps->chroma_format_idc == 3 ? 6u : 2u);
}

int avc_parse_pps_with_sets(const uint8_t *rbsp, size_t rbsp_size,
                            const avc_parameter_sets_t *sets,
                            avc_pps_t *pps)
{
    avc_bitreader_t br;

    *pps = (avc_pps_t){0};
    fill_flat_pps_scaling_lists(pps);
    avc_br_init(&br, rbsp, rbsp_size);

    pps->pic_parameter_set_id = avc_br_read_ue(&br);
    pps->seq_parameter_set_id = avc_br_read_ue(&br);
    if (pps->pic_parameter_set_id >= AVC_MAX_PPS || pps->seq_parameter_set_id >= AVC_MAX_SPS) {
        return 0;
    }
    pps->entropy_coding_mode_flag = (uint8_t)avc_br_read_bit(&br);
    pps->bottom_field_pic_order_in_frame_present_flag = (uint8_t)avc_br_read_bit(&br);
    pps->num_slice_groups_minus1 = avc_br_read_ue(&br);
    if (!parse_pps_slice_groups(&br, pps)) {
        return 0;
    }
    pps->num_ref_idx_l0_default_active_minus1 = avc_br_read_ue(&br);
    pps->num_ref_idx_l1_default_active_minus1 = avc_br_read_ue(&br);
    pps->weighted_pred_flag = (uint8_t)avc_br_read_bit(&br);
    pps->weighted_bipred_idc = (uint8_t)avc_br_read_bits(&br, 2);
    pps->pic_init_qp_minus26 = avc_br_read_se(&br);
    pps->pic_init_qs_minus26 = avc_br_read_se(&br);
    pps->chroma_qp_index_offset = avc_br_read_se(&br);
    pps->deblocking_filter_control_present_flag = (uint8_t)avc_br_read_bit(&br);
    pps->constrained_intra_pred_flag = (uint8_t)avc_br_read_bit(&br);
    pps->redundant_pic_cnt_present_flag = (uint8_t)avc_br_read_bit(&br);

    if (avc_br_more_rbsp_data(&br)) {
        uint32_t scaling_list_count;

        pps->transform_8x8_mode_flag = (uint8_t)avc_br_read_bit(&br);
        scaling_list_count = pps_scaling_list_count(sets, pps);
        if (!parse_pps_scaling_matrices(&br, pps, scaling_list_count)) {
            return 0;
        }
        pps->second_chroma_qp_index_offset = avc_br_read_se(&br);
    } else {
        pps->second_chroma_qp_index_offset = pps->chroma_qp_index_offset;
    }

    if (br.error) {
        return 0;
    }
    pps->present = 1;
    return 1;
}

int avc_parse_pps(const uint8_t *rbsp, size_t rbsp_size, avc_pps_t *pps)
{
    return avc_parse_pps_with_sets(rbsp, rbsp_size, NULL, pps);
}

static avc_slice_kind_t normalize_slice_type(uint32_t slice_type)
{
    return (avc_slice_kind_t)(slice_type % 5u);
}

static int parse_ref_pic_list_modifications_for_list(avc_bitreader_t *br,
                                                     avc_ref_pic_list_modification_t *mods,
                                                     uint32_t *count)
{
    *count = 0;
    for (;;) {
        avc_ref_pic_list_modification_t mod;

        if (*count >= AVC_MAX_REF_LIST_MODS) {
            return 0;
        }
        mod = (avc_ref_pic_list_modification_t){0};
        mod.modification_of_pic_nums_idc = avc_br_read_ue(br);
        if (br->error) {
            return 0;
        }
        if (mod.modification_of_pic_nums_idc == 3) {
            return 1;
        }
        if (mod.modification_of_pic_nums_idc > 3) {
            br->error = 1;
            return 0;
        }
        if (mod.modification_of_pic_nums_idc == 0 || mod.modification_of_pic_nums_idc == 1) {
            mod.abs_diff_pic_num_minus1 = avc_br_read_ue(br);
        } else if (mod.modification_of_pic_nums_idc == 2) {
            mod.long_term_pic_num = avc_br_read_ue(br);
        }
        if (br->error) {
            return 0;
        }
        mods[*count] = mod;
        (*count)++;
    }
}

static int parse_ref_pic_list_modification(avc_bitreader_t *br,
                                           const avc_slice_header_t *slice,
                                           avc_slice_header_t *out)
{
    if (slice->slice_kind == AVC_SLICE_I || slice->slice_kind == AVC_SLICE_SI) {
        return 1;
    }

    out->ref_pic_list_modification_flag_l0 = (uint8_t)avc_br_read_bit(br);
    if (out->ref_pic_list_modification_flag_l0) {
        if (!parse_ref_pic_list_modifications_for_list(br,
                                                       out->ref_pic_list_modifications_l0,
                                                       &out->ref_pic_list_modification_count_l0)) {
            return 0;
        }
    }
    if (slice->slice_kind == AVC_SLICE_B) {
        out->ref_pic_list_modification_flag_l1 = (uint8_t)avc_br_read_bit(br);
        if (out->ref_pic_list_modification_flag_l1) {
            if (!parse_ref_pic_list_modifications_for_list(br,
                                                           out->ref_pic_list_modifications_l1,
                                                           &out->ref_pic_list_modification_count_l1)) {
                return 0;
            }
        }
    }
    return !br->error;
}

static int parse_pred_weight_list(avc_bitreader_t *br,
                                  uint32_t num_ref_idx_active_minus1,
                                  uint32_t chroma_format_idc,
                                  uint32_t luma_log2_weight_denom,
                                  uint32_t chroma_log2_weight_denom,
                                  avc_pred_weight_t *weights)
{
    uint32_t i;

    if (num_ref_idx_active_minus1 >= AVC_MAX_REF_IDX_ACTIVE) {
        br->error = 1;
        return 0;
    }

    for (i = 0; i <= num_ref_idx_active_minus1; i++) {
        uint32_t j;

        weights[i].luma_weight = 1 << luma_log2_weight_denom;
        weights[i].luma_weight_flag = (uint8_t)avc_br_read_bit(br);
        if (weights[i].luma_weight_flag) {
            weights[i].luma_weight = avc_br_read_se(br);
            weights[i].luma_offset = avc_br_read_se(br);
        }
        if (chroma_format_idc != 0) {
            uint8_t chroma_weight_flag = (uint8_t)avc_br_read_bit(br);
            for (j = 0; j < 2; j++) {
                weights[i].chroma_weight_flag[j] = chroma_weight_flag;
                weights[i].chroma_weight[j] = 1 << chroma_log2_weight_denom;
            }
            if (chroma_weight_flag) {
                for (j = 0; j < 2; j++) {
                    weights[i].chroma_weight[j] = avc_br_read_se(br);
                    weights[i].chroma_offset[j] = avc_br_read_se(br);
                }
            }
        }
        if (br->error) {
            return 0;
        }
    }
    return 1;
}

static int parse_pred_weight_table(avc_bitreader_t *br,
                                   const avc_sps_t *sps,
                                   const avc_slice_header_t *slice,
                                   avc_slice_header_t *out)
{
    unsigned chroma_array_type = sps_chroma_array_type(sps);

    out->pred_weight_table_present = 1;
    out->luma_log2_weight_denom = avc_br_read_ue(br);
    if (chroma_array_type != 0) {
        out->chroma_log2_weight_denom = avc_br_read_ue(br);
    }
    if (br->error || out->luma_log2_weight_denom > 7 ||
        (chroma_array_type != 0 && out->chroma_log2_weight_denom > 7)) {
        br->error = 1;
        return 0;
    }

    if (!parse_pred_weight_list(br, slice->num_ref_idx_l0_active_minus1,
                                chroma_array_type,
                                out->luma_log2_weight_denom,
                                out->chroma_log2_weight_denom,
                                out->pred_weight_l0)) {
        return 0;
    }
    if (slice->slice_kind == AVC_SLICE_B) {
        if (!parse_pred_weight_list(br, slice->num_ref_idx_l1_active_minus1,
                                    chroma_array_type,
                                    out->luma_log2_weight_denom,
                                    out->chroma_log2_weight_denom,
                                    out->pred_weight_l1)) {
            return 0;
        }
    }
    return !br->error;
}

static int parse_dec_ref_pic_marking(avc_bitreader_t *br,
                                     avc_nal_header_t nal,
                                     avc_slice_header_t *slice)
{
    if (nal.nal_ref_idc == 0) {
        return 1;
    }

    if (nal.nal_unit_type == AVC_NAL_SLICE_IDR) {
        slice->no_output_of_prior_pics_flag = (uint8_t)avc_br_read_bit(br);
        slice->long_term_reference_flag = (uint8_t)avc_br_read_bit(br);
        return !br->error;
    }

    slice->adaptive_ref_pic_marking_mode_flag = (uint8_t)avc_br_read_bit(br);
    if (!slice->adaptive_ref_pic_marking_mode_flag) {
        return !br->error;
    }

    for (;;) {
        avc_dec_ref_pic_marking_t marking;

        if (slice->dec_ref_pic_marking_count >= AVC_MAX_MMCO) {
            br->error = 1;
            return 0;
        }
        marking = (avc_dec_ref_pic_marking_t){0};
        marking.memory_management_control_operation = avc_br_read_ue(br);
        if (br->error) {
            return 0;
        }
        if (marking.memory_management_control_operation == 0) {
            return 1;
        }
        switch (marking.memory_management_control_operation) {
        case 1:
        case 3:
            marking.difference_of_pic_nums_minus1 = avc_br_read_ue(br);
            break;
        case 2:
            marking.long_term_pic_num = avc_br_read_ue(br);
            break;
        case 4:
            marking.max_long_term_frame_idx_plus1 = avc_br_read_ue(br);
            break;
        case 5:
            break;
        case 6:
            marking.long_term_frame_idx = avc_br_read_ue(br);
            break;
        default:
            br->error = 1;
            return 0;
        }
        if (marking.memory_management_control_operation == 3) {
            marking.long_term_frame_idx = avc_br_read_ue(br);
        }
        if (br->error) {
            return 0;
        }
        slice->dec_ref_pic_marking[slice->dec_ref_pic_marking_count++] = marking;
    }
}

int avc_parse_slice_header(const uint8_t *rbsp, size_t rbsp_size,
                           avc_nal_header_t nal,
                           const avc_parameter_sets_t *sets,
                           avc_slice_header_t *slice)
{
    avc_bitreader_t br;
    const avc_pps_t *pps;
    const avc_sps_t *sps;

    *slice = (avc_slice_header_t){0};
    slice->nal = nal;
    avc_br_init(&br, rbsp, rbsp_size);

    slice->first_mb_in_slice = avc_br_read_ue(&br);
    slice->slice_type = avc_br_read_ue(&br);
    slice->slice_kind = normalize_slice_type(slice->slice_type);
    slice->pic_parameter_set_id = avc_br_read_ue(&br);
    if (slice->pic_parameter_set_id >= AVC_MAX_PPS || !sets->pps[slice->pic_parameter_set_id].present) {
        return 0;
    }
    pps = &sets->pps[slice->pic_parameter_set_id];
    if (pps->seq_parameter_set_id >= AVC_MAX_SPS || !sets->sps[pps->seq_parameter_set_id].present) {
        return 0;
    }
    sps = &sets->sps[pps->seq_parameter_set_id];

    slice->frame_num = avc_br_read_bits(&br, sps->log2_max_frame_num_minus4 + 4u);
    if (!sps->frame_mbs_only_flag) {
        slice->field_pic_flag = (uint8_t)avc_br_read_bit(&br);
        if (slice->field_pic_flag) {
            slice->bottom_field_flag = (uint8_t)avc_br_read_bit(&br);
        }
    }
    if (nal.nal_unit_type == AVC_NAL_SLICE_IDR) {
        slice->idr_pic_id = avc_br_read_ue(&br);
    }
    if (sps->pic_order_cnt_type == 0) {
        slice->pic_order_cnt_lsb = avc_br_read_bits(&br, sps->log2_max_pic_order_cnt_lsb_minus4 + 4u);
        if (pps->bottom_field_pic_order_in_frame_present_flag && !slice->field_pic_flag) {
            slice->delta_pic_order_cnt_bottom = avc_br_read_se(&br);
        }
    } else if (sps->pic_order_cnt_type == 1) {
        slice->delta_pic_order_cnt[0] = avc_br_read_se(&br);
        if (pps->bottom_field_pic_order_in_frame_present_flag && !slice->field_pic_flag) {
            slice->delta_pic_order_cnt[1] = avc_br_read_se(&br);
        }
    }
    if (pps->redundant_pic_cnt_present_flag) {
        slice->redundant_pic_cnt = avc_br_read_ue(&br);
    }
    if (slice->slice_kind == AVC_SLICE_B) {
        slice->direct_spatial_mv_pred_flag = (uint8_t)avc_br_read_bit(&br);
    }
    if (slice->slice_kind == AVC_SLICE_P || slice->slice_kind == AVC_SLICE_SP || slice->slice_kind == AVC_SLICE_B) {
        slice->num_ref_idx_active_override_flag = (uint8_t)avc_br_read_bit(&br);
        if (slice->num_ref_idx_active_override_flag) {
            slice->num_ref_idx_l0_active_minus1 = avc_br_read_ue(&br);
            if (slice->slice_kind == AVC_SLICE_B) {
                slice->num_ref_idx_l1_active_minus1 = avc_br_read_ue(&br);
            }
        } else {
            slice->num_ref_idx_l0_active_minus1 = pps->num_ref_idx_l0_default_active_minus1;
            slice->num_ref_idx_l1_active_minus1 = pps->num_ref_idx_l1_default_active_minus1;
        }
    }

    if (!parse_ref_pic_list_modification(&br, slice, slice)) {
        return 0;
    }
    if ((pps->weighted_pred_flag &&
         (slice->slice_kind == AVC_SLICE_P || slice->slice_kind == AVC_SLICE_SP)) ||
        (pps->weighted_bipred_idc == 1 && slice->slice_kind == AVC_SLICE_B)) {
        if (!parse_pred_weight_table(&br, sps, slice, slice)) {
            return 0;
        }
    }
    if (!parse_dec_ref_pic_marking(&br, nal, slice)) {
        return 0;
    }
    if (pps->entropy_coding_mode_flag && slice->slice_kind != AVC_SLICE_I && slice->slice_kind != AVC_SLICE_SI) {
        slice->cabac_init_idc = avc_br_read_ue(&br);
    }
    slice->slice_qp_delta = avc_br_read_se(&br);
    if (pps->deblocking_filter_control_present_flag) {
        slice->disable_deblocking_filter_idc = avc_br_read_ue(&br);
        if (slice->disable_deblocking_filter_idc != 1) {
            slice->slice_alpha_c0_offset_div2 = avc_br_read_se(&br);
            slice->slice_beta_offset_div2 = avc_br_read_se(&br);
        }
    }

    if (br.error) {
        return 0;
    }
    slice->valid = 1;
    slice->header_bits = br.bit_pos;
    return 1;
}

const char *avc_slice_kind_name(avc_slice_kind_t kind)
{
    switch (kind) {
    case AVC_SLICE_P: return "P";
    case AVC_SLICE_B: return "B";
    case AVC_SLICE_I: return "I";
    case AVC_SLICE_SP: return "SP";
    case AVC_SLICE_SI: return "SI";
    default: return "?";
    }
}

uint32_t avc_sps_width(const avc_sps_t *sps)
{
    uint32_t width = (sps->pic_width_in_mbs_minus1 + 1u) * 16u;
    uint32_t chroma_array_type = sps_chroma_array_type(sps);
    uint32_t crop_unit_x = 1u;

    if (chroma_array_type == 1 || chroma_array_type == 2) {
        crop_unit_x = 2u;
    }
    if (sps->frame_cropping_flag) {
        width -= (sps->frame_crop_left_offset + sps->frame_crop_right_offset) * crop_unit_x;
    }
    return width;
}

uint32_t avc_sps_height(const avc_sps_t *sps)
{
    uint32_t frame_height_in_mbs = (2u - sps->frame_mbs_only_flag) * (sps->pic_height_in_map_units_minus1 + 1u);
    uint32_t height = frame_height_in_mbs * 16u;
    uint32_t chroma_array_type = sps_chroma_array_type(sps);
    uint32_t crop_unit_y = 2u - sps->frame_mbs_only_flag;

    if (chroma_array_type == 1) {
        crop_unit_y *= 2u;
    }
    if (sps->frame_cropping_flag) {
        height -= (sps->frame_crop_top_offset + sps->frame_crop_bottom_offset) * crop_unit_y;
    }
    return height;
}
