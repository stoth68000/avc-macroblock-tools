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

static void skip_scaling_list(avc_bitreader_t *br, int size)
{
    int last_scale = 8;
    int next_scale = 8;
    int j;

    for (j = 0; j < size; j++) {
        if (next_scale != 0) {
            int delta_scale = avc_br_read_se(br);
            next_scale = (last_scale + delta_scale + 256) % 256;
        }
        last_scale = next_scale == 0 ? last_scale : next_scale;
    }
}

int avc_parse_sps(const uint8_t *rbsp, size_t rbsp_size, avc_sps_t *sps)
{
    avc_bitreader_t br;
    uint32_t i;

    *sps = (avc_sps_t){0};
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
        if (sps->chroma_format_idc == 3) {
            avc_br_read_bit(&br);
        }
        sps->bit_depth_luma_minus8 = avc_br_read_ue(&br);
        sps->bit_depth_chroma_minus8 = avc_br_read_ue(&br);
        avc_br_read_bit(&br);
        if (avc_br_read_bit(&br)) {
            uint32_t count = sps->chroma_format_idc != 3 ? 8u : 12u;
            for (i = 0; i < count; i++) {
                if (avc_br_read_bit(&br)) {
                    skip_scaling_list(&br, i < 6 ? 16 : 64);
                }
            }
        }
    }

    sps->log2_max_frame_num_minus4 = avc_br_read_ue(&br);
    sps->pic_order_cnt_type = avc_br_read_ue(&br);
    if (sps->pic_order_cnt_type == 0) {
        sps->log2_max_pic_order_cnt_lsb_minus4 = avc_br_read_ue(&br);
    } else if (sps->pic_order_cnt_type == 1) {
        uint32_t num_ref_frames_in_pic_order_cnt_cycle;
        avc_br_read_bit(&br);
        avc_br_read_se(&br);
        avc_br_read_se(&br);
        num_ref_frames_in_pic_order_cnt_cycle = avc_br_read_ue(&br);
        for (i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; i++) {
            avc_br_read_se(&br);
        }
    } else if (sps->pic_order_cnt_type > 2) {
        return 0;
    }

    sps->max_num_ref_frames = avc_br_read_ue(&br);
    avc_br_read_bit(&br);
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

    if (br.error) {
        return 0;
    }
    sps->present = 1;
    return 1;
}

int avc_parse_pps(const uint8_t *rbsp, size_t rbsp_size, avc_pps_t *pps)
{
    avc_bitreader_t br;

    *pps = (avc_pps_t){0};
    avc_br_init(&br, rbsp, rbsp_size);

    pps->pic_parameter_set_id = avc_br_read_ue(&br);
    pps->seq_parameter_set_id = avc_br_read_ue(&br);
    if (pps->pic_parameter_set_id >= AVC_MAX_PPS || pps->seq_parameter_set_id >= AVC_MAX_SPS) {
        return 0;
    }
    pps->entropy_coding_mode_flag = (uint8_t)avc_br_read_bit(&br);
    pps->bottom_field_pic_order_in_frame_present_flag = (uint8_t)avc_br_read_bit(&br);
    pps->num_slice_groups_minus1 = avc_br_read_ue(&br);
    if (pps->num_slice_groups_minus1 != 0) {
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
        pps->transform_8x8_mode_flag = (uint8_t)avc_br_read_bit(&br);
        if (avc_br_read_bit(&br)) {
            return 0;
        }
        pps->chroma_qp_index_offset = avc_br_read_se(&br);
    }

    if (br.error) {
        return 0;
    }
    pps->present = 1;
    return 1;
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
    out->pred_weight_table_present = 1;
    out->luma_log2_weight_denom = avc_br_read_ue(br);
    if (sps->chroma_format_idc != 0) {
        out->chroma_log2_weight_denom = avc_br_read_ue(br);
    }
    if (br->error || out->luma_log2_weight_denom > 7 ||
        (sps->chroma_format_idc != 0 && out->chroma_log2_weight_denom > 7)) {
        br->error = 1;
        return 0;
    }

    if (!parse_pred_weight_list(br, slice->num_ref_idx_l0_active_minus1,
                                sps->chroma_format_idc,
                                out->luma_log2_weight_denom,
                                out->chroma_log2_weight_denom,
                                out->pred_weight_l0)) {
        return 0;
    }
    if (slice->slice_kind == AVC_SLICE_B) {
        if (!parse_pred_weight_list(br, slice->num_ref_idx_l1_active_minus1,
                                    sps->chroma_format_idc,
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
    uint32_t crop_unit_x = sps->chroma_format_idc == 0 ? 1u : 2u;
    if (sps->frame_cropping_flag) {
        width -= (sps->frame_crop_left_offset + sps->frame_crop_right_offset) * crop_unit_x;
    }
    return width;
}

uint32_t avc_sps_height(const avc_sps_t *sps)
{
    uint32_t frame_height_in_mbs = (2u - sps->frame_mbs_only_flag) * (sps->pic_height_in_map_units_minus1 + 1u);
    uint32_t height = frame_height_in_mbs * 16u;
    uint32_t crop_unit_y = sps->chroma_format_idc == 0 ? (2u - sps->frame_mbs_only_flag) : 2u * (2u - sps->frame_mbs_only_flag);
    if (sps->frame_cropping_flag) {
        height -= (sps->frame_crop_top_offset + sps->frame_crop_bottom_offset) * crop_unit_y;
    }
    return height;
}
