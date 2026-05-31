#ifndef AVC_SYNTAX_H
#define AVC_SYNTAX_H

#include <stddef.h>
#include <stdint.h>
#include "avc/avc_nal.h"

#define AVC_MAX_SPS 32
#define AVC_MAX_PPS 256
#define AVC_MAX_REF_LIST_MODS 64
#define AVC_MAX_REF_IDX_ACTIVE 32
#define AVC_MAX_MMCO 64
#define AVC_SPS_SCALING_LIST_4X4_COUNT 6
#define AVC_SPS_SCALING_LIST_8X8_COUNT 6
#define AVC_MAX_HRD_CPB_CNT 32
#define AVC_MAX_SLICE_GROUPS 8
#define AVC_MAX_SLICE_GROUP_ID 8192

typedef enum {
    AVC_SLICE_P = 0,
    AVC_SLICE_B = 1,
    AVC_SLICE_I = 2,
    AVC_SLICE_SP = 3,
    AVC_SLICE_SI = 4
} avc_slice_kind_t;

typedef struct {
    uint8_t present;
    uint32_t cpb_cnt_minus1;
    uint8_t bit_rate_scale;
    uint8_t cpb_size_scale;
    uint32_t bit_rate_value_minus1[AVC_MAX_HRD_CPB_CNT];
    uint32_t cpb_size_value_minus1[AVC_MAX_HRD_CPB_CNT];
    uint8_t cbr_flag[AVC_MAX_HRD_CPB_CNT];
    uint8_t initial_cpb_removal_delay_length_minus1;
    uint8_t cpb_removal_delay_length_minus1;
    uint8_t dpb_output_delay_length_minus1;
    uint8_t time_offset_length;
} avc_hrd_parameters_t;

typedef struct {
    uint8_t aspect_ratio_info_present_flag;
    uint8_t aspect_ratio_idc;
    uint16_t sar_width;
    uint16_t sar_height;
    uint8_t overscan_info_present_flag;
    uint8_t overscan_appropriate_flag;
    uint8_t video_signal_type_present_flag;
    uint8_t video_format;
    uint8_t video_full_range_flag;
    uint8_t colour_description_present_flag;
    uint8_t colour_primaries;
    uint8_t transfer_characteristics;
    uint8_t matrix_coefficients;
    uint8_t chroma_loc_info_present_flag;
    uint32_t chroma_sample_loc_type_top_field;
    uint32_t chroma_sample_loc_type_bottom_field;
    uint8_t timing_info_present_flag;
    uint32_t num_units_in_tick;
    uint32_t time_scale;
    uint8_t fixed_frame_rate_flag;
    uint8_t nal_hrd_parameters_present_flag;
    avc_hrd_parameters_t nal_hrd_parameters;
    uint8_t vcl_hrd_parameters_present_flag;
    avc_hrd_parameters_t vcl_hrd_parameters;
    uint8_t low_delay_hrd_flag;
    uint8_t pic_struct_present_flag;
    uint8_t bitstream_restriction_flag;
    uint8_t motion_vectors_over_pic_boundaries_flag;
    uint32_t max_bytes_per_pic_denom;
    uint32_t max_bits_per_mb_denom;
    uint32_t log2_max_mv_length_horizontal;
    uint32_t log2_max_mv_length_vertical;
    uint32_t max_num_reorder_frames;
    uint32_t max_dec_frame_buffering;
} avc_vui_parameters_t;

typedef struct {
    int present;
    uint8_t profile_idc;
    uint8_t constraint_set_flags;
    uint8_t level_idc;
    uint32_t seq_parameter_set_id;
    uint32_t chroma_format_idc;
    uint8_t separate_colour_plane_flag;
    uint32_t bit_depth_luma_minus8;
    uint32_t bit_depth_chroma_minus8;
    uint8_t qpprime_y_zero_transform_bypass_flag;
    uint8_t seq_scaling_matrix_present_flag;
    uint8_t seq_scaling_list_present_flag[12];
    uint8_t use_default_scaling_matrix_flag[12];
    uint8_t scaling_list_4x4[AVC_SPS_SCALING_LIST_4X4_COUNT][16];
    uint8_t scaling_list_8x8[AVC_SPS_SCALING_LIST_8X8_COUNT][64];
    uint32_t log2_max_frame_num_minus4;
    uint32_t pic_order_cnt_type;
    uint32_t log2_max_pic_order_cnt_lsb_minus4;
    uint8_t delta_pic_order_always_zero_flag;
    int32_t offset_for_non_ref_pic;
    int32_t offset_for_top_to_bottom_field;
    uint32_t num_ref_frames_in_pic_order_cnt_cycle;
    int32_t offset_for_ref_frame[256];
    uint32_t max_num_ref_frames;
    uint8_t gaps_in_frame_num_value_allowed_flag;
    uint32_t pic_width_in_mbs_minus1;
    uint32_t pic_height_in_map_units_minus1;
    uint8_t frame_mbs_only_flag;
    uint8_t mb_adaptive_frame_field_flag;
    uint8_t direct_8x8_inference_flag;
    uint8_t frame_cropping_flag;
    uint32_t frame_crop_left_offset;
    uint32_t frame_crop_right_offset;
    uint32_t frame_crop_top_offset;
    uint32_t frame_crop_bottom_offset;
    uint8_t vui_parameters_present_flag;
    avc_vui_parameters_t vui;
} avc_sps_t;

typedef struct {
    int present;
    uint32_t pic_parameter_set_id;
    uint32_t seq_parameter_set_id;
    uint8_t entropy_coding_mode_flag;
    uint8_t bottom_field_pic_order_in_frame_present_flag;
    uint32_t num_slice_groups_minus1;
    uint32_t slice_group_map_type;
    uint32_t run_length_minus1[AVC_MAX_SLICE_GROUPS];
    uint32_t top_left[AVC_MAX_SLICE_GROUPS - 1];
    uint32_t bottom_right[AVC_MAX_SLICE_GROUPS - 1];
    uint8_t slice_group_change_direction_flag;
    uint32_t slice_group_change_rate_minus1;
    uint32_t pic_size_in_map_units_minus1;
    uint32_t slice_group_id_count;
    uint8_t slice_group_id[AVC_MAX_SLICE_GROUP_ID];
    uint8_t slice_group_id_truncated;
    uint32_t num_ref_idx_l0_default_active_minus1;
    uint32_t num_ref_idx_l1_default_active_minus1;
    uint8_t weighted_pred_flag;
    uint8_t weighted_bipred_idc;
    int32_t pic_init_qp_minus26;
    int32_t pic_init_qs_minus26;
    int32_t chroma_qp_index_offset;
    uint8_t deblocking_filter_control_present_flag;
    uint8_t constrained_intra_pred_flag;
    uint8_t redundant_pic_cnt_present_flag;
    uint8_t transform_8x8_mode_flag;
    uint8_t pic_scaling_matrix_present_flag;
    uint8_t pic_scaling_list_present_flag[12];
    uint8_t use_default_scaling_matrix_flag[12];
    uint8_t scaling_list_4x4[AVC_SPS_SCALING_LIST_4X4_COUNT][16];
    uint8_t scaling_list_8x8[AVC_SPS_SCALING_LIST_8X8_COUNT][64];
    int32_t second_chroma_qp_index_offset;
} avc_pps_t;

typedef struct {
    uint32_t modification_of_pic_nums_idc;
    uint32_t abs_diff_pic_num_minus1;
    uint32_t long_term_pic_num;
} avc_ref_pic_list_modification_t;

typedef struct {
    uint8_t luma_weight_flag;
    int32_t luma_weight;
    int32_t luma_offset;
    uint8_t chroma_weight_flag[2];
    int32_t chroma_weight[2];
    int32_t chroma_offset[2];
} avc_pred_weight_t;

typedef struct {
    uint32_t memory_management_control_operation;
    uint32_t difference_of_pic_nums_minus1;
    uint32_t long_term_pic_num;
    uint32_t long_term_frame_idx;
    uint32_t max_long_term_frame_idx_plus1;
} avc_dec_ref_pic_marking_t;

typedef struct {
    int valid;
    avc_nal_header_t nal;
    uint32_t first_mb_in_slice;
    uint32_t slice_type;
    avc_slice_kind_t slice_kind;
    uint32_t pic_parameter_set_id;
    uint32_t frame_num;
    uint8_t field_pic_flag;
    uint8_t bottom_field_flag;
    uint32_t idr_pic_id;
    uint32_t pic_order_cnt_lsb;
    int32_t delta_pic_order_cnt_bottom;
    int32_t delta_pic_order_cnt[2];
    uint32_t redundant_pic_cnt;
    uint8_t direct_spatial_mv_pred_flag;
    uint8_t num_ref_idx_active_override_flag;
    uint32_t num_ref_idx_l0_active_minus1;
    uint32_t num_ref_idx_l1_active_minus1;
    uint8_t ref_pic_list_modification_flag_l0;
    uint8_t ref_pic_list_modification_flag_l1;
    uint32_t ref_pic_list_modification_count_l0;
    uint32_t ref_pic_list_modification_count_l1;
    avc_ref_pic_list_modification_t ref_pic_list_modifications_l0[AVC_MAX_REF_LIST_MODS];
    avc_ref_pic_list_modification_t ref_pic_list_modifications_l1[AVC_MAX_REF_LIST_MODS];
    uint8_t pred_weight_table_present;
    uint32_t luma_log2_weight_denom;
    uint32_t chroma_log2_weight_denom;
    avc_pred_weight_t pred_weight_l0[AVC_MAX_REF_IDX_ACTIVE];
    avc_pred_weight_t pred_weight_l1[AVC_MAX_REF_IDX_ACTIVE];
    uint8_t no_output_of_prior_pics_flag;
    uint8_t long_term_reference_flag;
    uint8_t adaptive_ref_pic_marking_mode_flag;
    uint32_t dec_ref_pic_marking_count;
    avc_dec_ref_pic_marking_t dec_ref_pic_marking[AVC_MAX_MMCO];
    uint32_t cabac_init_idc;
    int32_t slice_qp_delta;
    uint32_t disable_deblocking_filter_idc;
    int32_t slice_alpha_c0_offset_div2;
    int32_t slice_beta_offset_div2;
    size_t header_bits;
} avc_slice_header_t;

typedef struct {
    avc_sps_t sps[AVC_MAX_SPS];
    avc_pps_t pps[AVC_MAX_PPS];
} avc_parameter_sets_t;

int avc_parse_sps(const uint8_t *rbsp, size_t rbsp_size, avc_sps_t *sps);
int avc_parse_pps(const uint8_t *rbsp, size_t rbsp_size, avc_pps_t *pps);
int avc_parse_pps_with_sets(const uint8_t *rbsp, size_t rbsp_size,
                            const avc_parameter_sets_t *sets,
                            avc_pps_t *pps);
int avc_parse_slice_header(const uint8_t *rbsp, size_t rbsp_size,
                           avc_nal_header_t nal,
                           const avc_parameter_sets_t *sets,
                           avc_slice_header_t *slice);
const char *avc_slice_kind_name(avc_slice_kind_t kind);
uint32_t avc_sps_width(const avc_sps_t *sps);
uint32_t avc_sps_height(const avc_sps_t *sps);

#endif
