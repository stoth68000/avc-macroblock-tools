#include "avc/avc_syntax.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint8_t data[256];
    size_t bit_pos;
} bit_writer_t;

static void bw_put_ue(bit_writer_t *bw, uint32_t value);

static void bw_put_bit(bit_writer_t *bw, unsigned bit)
{
    if (bit) {
        bw->data[bw->bit_pos >> 3] |= (uint8_t)(1u << (7u - (bw->bit_pos & 7u)));
    }
    bw->bit_pos++;
}

static void bw_put_bits(bit_writer_t *bw, uint32_t value, unsigned bits)
{
    unsigned i;

    for (i = 0; i < bits; i++) {
        bw_put_bit(bw, (value >> (bits - 1u - i)) & 1u);
    }
}

static void bw_put_hrd(bit_writer_t *bw)
{
    bw_put_ue(bw, 1);
    bw_put_bits(bw, 3, 4);
    bw_put_bits(bw, 4, 4);
    bw_put_ue(bw, 10);
    bw_put_ue(bw, 20);
    bw_put_bit(bw, 1);
    bw_put_ue(bw, 11);
    bw_put_ue(bw, 21);
    bw_put_bit(bw, 0);
    bw_put_bits(bw, 23, 5);
    bw_put_bits(bw, 24, 5);
    bw_put_bits(bw, 25, 5);
    bw_put_bits(bw, 6, 5);
}

static void bw_put_ue(bit_writer_t *bw, uint32_t value)
{
    uint32_t code_num = value + 1u;
    unsigned bits = 0;
    unsigned leading_zero_bits;
    uint32_t tmp = code_num;
    unsigned i;

    while (tmp != 0) {
        bits++;
        tmp >>= 1;
    }
    leading_zero_bits = bits - 1u;
    for (i = 0; i < leading_zero_bits; i++) {
        bw_put_bit(bw, 0);
    }
    bw_put_bits(bw, code_num, bits);
}

static void bw_put_se(bit_writer_t *bw, int32_t value)
{
    uint32_t code_num = value <= 0 ? (uint32_t)(-value * 2) : (uint32_t)(value * 2 - 1);

    bw_put_ue(bw, code_num);
}

static size_t bw_finish_rbsp(bit_writer_t *bw)
{
    bw_put_bit(bw, 1);
    while ((bw->bit_pos & 7u) != 0) {
        bw_put_bit(bw, 0);
    }
    return bw->bit_pos >> 3;
}

static void test_sps_scaling_matrices(void)
{
    bit_writer_t bw;
    avc_sps_t sps;
    size_t size;
    unsigned i;

    bw = (bit_writer_t){0};
    bw_put_bits(&bw, 100, 8);
    bw_put_bits(&bw, 0, 8);
    bw_put_bits(&bw, 30, 8);
    bw_put_ue(&bw, 0);
    bw_put_ue(&bw, 1);
    bw_put_ue(&bw, 0);
    bw_put_ue(&bw, 0);
    bw_put_bit(&bw, 0);
    bw_put_bit(&bw, 1);

    bw_put_bit(&bw, 1);
    for (i = 0; i < 16; i++) {
        bw_put_se(&bw, 0);
    }
    bw_put_bit(&bw, 0);
    bw_put_bit(&bw, 0);
    bw_put_bit(&bw, 1);
    bw_put_se(&bw, -8);
    bw_put_bit(&bw, 0);
    bw_put_bit(&bw, 0);
    bw_put_bit(&bw, 0);
    bw_put_bit(&bw, 0);

    bw_put_ue(&bw, 0);
    bw_put_ue(&bw, 0);
    bw_put_ue(&bw, 0);
    bw_put_ue(&bw, 1);
    bw_put_bit(&bw, 0);
    bw_put_ue(&bw, 0);
    bw_put_ue(&bw, 0);
    bw_put_bit(&bw, 1);
    bw_put_bit(&bw, 1);
    bw_put_bit(&bw, 0);
    bw_put_bit(&bw, 0);
    size = bw_finish_rbsp(&bw);

    assert(avc_parse_sps(bw.data, size, &sps));
    assert(sps.profile_idc == 100);
    assert(sps.seq_scaling_matrix_present_flag == 1);
    assert(sps.seq_scaling_list_present_flag[0] == 1);
    assert(sps.seq_scaling_list_present_flag[1] == 0);
    assert(sps.seq_scaling_list_present_flag[3] == 1);
    assert(sps.use_default_scaling_matrix_flag[3] == 1);
    assert(sps.scaling_list_4x4[0][0] == 8);
    assert(sps.scaling_list_4x4[0][15] == 8);
    assert(sps.scaling_list_4x4[1][0] == 8);
    assert(sps.scaling_list_4x4[3][0] == 10);
    assert(sps.scaling_list_4x4[3][15] == 34);
    assert(sps.scaling_list_8x8[0][0] == 6);
    assert(sps.scaling_list_8x8[0][63] == 42);
    assert(sps.scaling_list_8x8[1][0] == 9);
    assert(sps.scaling_list_8x8[1][63] == 35);
}

static size_t write_minimal_high_sps(bit_writer_t *bw,
                                     uint32_t chroma_format_idc,
                                     unsigned separate_colour_plane_flag,
                                     uint32_t bit_depth_luma_minus8,
                                     uint32_t bit_depth_chroma_minus8,
                                     unsigned qpprime_y_zero_transform_bypass_flag,
                                     uint32_t log2_max_frame_num_minus4)
{
    bw_put_bits(bw, 244, 8);
    bw_put_bits(bw, 0, 8);
    bw_put_bits(bw, 40, 8);
    bw_put_ue(bw, 0);
    bw_put_ue(bw, chroma_format_idc);
    if (chroma_format_idc == 3) {
        bw_put_bit(bw, separate_colour_plane_flag);
    }
    bw_put_ue(bw, bit_depth_luma_minus8);
    bw_put_ue(bw, bit_depth_chroma_minus8);
    bw_put_bit(bw, qpprime_y_zero_transform_bypass_flag);
    bw_put_bit(bw, 0);
    bw_put_ue(bw, log2_max_frame_num_minus4);
    bw_put_ue(bw, 1);
    bw_put_bit(bw, 1);
    bw_put_se(bw, -2);
    bw_put_se(bw, 3);
    bw_put_ue(bw, 2);
    bw_put_se(bw, 1);
    bw_put_se(bw, -1);
    bw_put_ue(bw, 4);
    bw_put_bit(bw, 1);
    bw_put_ue(bw, 1);
    bw_put_ue(bw, 0);
    bw_put_bit(bw, 1);
    bw_put_bit(bw, 1);
    bw_put_bit(bw, 1);
    bw_put_ue(bw, 1);
    bw_put_ue(bw, 1);
    bw_put_ue(bw, 1);
    bw_put_ue(bw, 1);
    bw_put_bit(bw, 0);
    return bw_finish_rbsp(bw);
}

static void test_sps_high_profile_edge_fields(void)
{
    bit_writer_t bw;
    avc_sps_t sps;
    size_t size;

    bw = (bit_writer_t){0};
    size = write_minimal_high_sps(&bw, 3, 1, 6, 5, 1, 12);

    assert(avc_parse_sps(bw.data, size, &sps));
    assert(sps.profile_idc == 244);
    assert(sps.chroma_format_idc == 3);
    assert(sps.separate_colour_plane_flag == 1);
    assert(sps.bit_depth_luma_minus8 == 6);
    assert(sps.bit_depth_chroma_minus8 == 5);
    assert(sps.qpprime_y_zero_transform_bypass_flag == 1);
    assert(sps.log2_max_frame_num_minus4 == 12);
    assert(sps.pic_order_cnt_type == 1);
    assert(sps.delta_pic_order_always_zero_flag == 1);
    assert(sps.offset_for_non_ref_pic == -2);
    assert(sps.offset_for_top_to_bottom_field == 3);
    assert(sps.num_ref_frames_in_pic_order_cnt_cycle == 2);
    assert(sps.offset_for_ref_frame[0] == 1);
    assert(sps.offset_for_ref_frame[1] == -1);
    assert(sps.gaps_in_frame_num_value_allowed_flag == 1);
    assert(sps.frame_cropping_flag == 1);
    assert(avc_sps_width(&sps) == 30);
    assert(avc_sps_height(&sps) == 14);
}

static void test_sps_high_profile_validation(void)
{
    bit_writer_t bw;
    avc_sps_t sps;
    size_t size;

    bw = (bit_writer_t){0};
    size = write_minimal_high_sps(&bw, 4, 0, 0, 0, 0, 0);
    assert(!avc_parse_sps(bw.data, size, &sps));

    bw = (bit_writer_t){0};
    size = write_minimal_high_sps(&bw, 1, 0, 7, 0, 0, 0);
    assert(!avc_parse_sps(bw.data, size, &sps));

    bw = (bit_writer_t){0};
    size = write_minimal_high_sps(&bw, 1, 0, 0, 7, 0, 0);
    assert(!avc_parse_sps(bw.data, size, &sps));

    bw = (bit_writer_t){0};
    size = write_minimal_high_sps(&bw, 1, 0, 0, 0, 0, 13);
    assert(!avc_parse_sps(bw.data, size, &sps));
}

static void test_sps_vui_hrd(void)
{
    bit_writer_t bw;
    avc_sps_t sps;
    size_t size;

    bw = (bit_writer_t){0};
    bw_put_bits(&bw, 66, 8);
    bw_put_bits(&bw, 0, 8);
    bw_put_bits(&bw, 30, 8);
    bw_put_ue(&bw, 0);
    bw_put_ue(&bw, 0);
    bw_put_ue(&bw, 0);
    bw_put_ue(&bw, 0);
    bw_put_ue(&bw, 1);
    bw_put_bit(&bw, 0);
    bw_put_ue(&bw, 1);
    bw_put_ue(&bw, 0);
    bw_put_bit(&bw, 1);
    bw_put_bit(&bw, 1);
    bw_put_bit(&bw, 0);
    bw_put_bit(&bw, 1);

    bw_put_bit(&bw, 1);
    bw_put_bits(&bw, 255, 8);
    bw_put_bits(&bw, 4, 16);
    bw_put_bits(&bw, 3, 16);
    bw_put_bit(&bw, 1);
    bw_put_bit(&bw, 0);
    bw_put_bit(&bw, 1);
    bw_put_bits(&bw, 5, 3);
    bw_put_bit(&bw, 1);
    bw_put_bit(&bw, 1);
    bw_put_bits(&bw, 1, 8);
    bw_put_bits(&bw, 13, 8);
    bw_put_bits(&bw, 6, 8);
    bw_put_bit(&bw, 1);
    bw_put_ue(&bw, 2);
    bw_put_ue(&bw, 3);
    bw_put_bit(&bw, 1);
    bw_put_bits(&bw, 1001, 32);
    bw_put_bits(&bw, 60000, 32);
    bw_put_bit(&bw, 1);
    bw_put_bit(&bw, 1);
    bw_put_hrd(&bw);
    bw_put_bit(&bw, 1);
    bw_put_hrd(&bw);
    bw_put_bit(&bw, 1);
    bw_put_bit(&bw, 1);
    bw_put_bit(&bw, 1);
    bw_put_bit(&bw, 1);
    bw_put_ue(&bw, 2);
    bw_put_ue(&bw, 1);
    bw_put_ue(&bw, 16);
    bw_put_ue(&bw, 15);
    bw_put_ue(&bw, 2);
    bw_put_ue(&bw, 4);
    size = bw_finish_rbsp(&bw);

    assert(avc_parse_sps(bw.data, size, &sps));
    assert(sps.vui_parameters_present_flag == 1);
    assert(sps.vui.aspect_ratio_info_present_flag == 1);
    assert(sps.vui.aspect_ratio_idc == 255);
    assert(sps.vui.sar_width == 4);
    assert(sps.vui.sar_height == 3);
    assert(sps.vui.overscan_info_present_flag == 1);
    assert(sps.vui.overscan_appropriate_flag == 0);
    assert(sps.vui.video_signal_type_present_flag == 1);
    assert(sps.vui.video_format == 5);
    assert(sps.vui.video_full_range_flag == 1);
    assert(sps.vui.colour_description_present_flag == 1);
    assert(sps.vui.colour_primaries == 1);
    assert(sps.vui.transfer_characteristics == 13);
    assert(sps.vui.matrix_coefficients == 6);
    assert(sps.vui.chroma_loc_info_present_flag == 1);
    assert(sps.vui.chroma_sample_loc_type_top_field == 2);
    assert(sps.vui.chroma_sample_loc_type_bottom_field == 3);
    assert(sps.vui.timing_info_present_flag == 1);
    assert(sps.vui.num_units_in_tick == 1001);
    assert(sps.vui.time_scale == 60000);
    assert(sps.vui.fixed_frame_rate_flag == 1);
    assert(sps.vui.nal_hrd_parameters_present_flag == 1);
    assert(sps.vui.vcl_hrd_parameters_present_flag == 1);
    assert(sps.vui.nal_hrd_parameters.cpb_cnt_minus1 == 1);
    assert(sps.vui.nal_hrd_parameters.bit_rate_scale == 3);
    assert(sps.vui.nal_hrd_parameters.cpb_size_scale == 4);
    assert(sps.vui.nal_hrd_parameters.bit_rate_value_minus1[1] == 11);
    assert(sps.vui.nal_hrd_parameters.cpb_size_value_minus1[1] == 21);
    assert(sps.vui.nal_hrd_parameters.cbr_flag[0] == 1);
    assert(sps.vui.nal_hrd_parameters.initial_cpb_removal_delay_length_minus1 == 23);
    assert(sps.vui.nal_hrd_parameters.cpb_removal_delay_length_minus1 == 24);
    assert(sps.vui.nal_hrd_parameters.dpb_output_delay_length_minus1 == 25);
    assert(sps.vui.nal_hrd_parameters.time_offset_length == 6);
    assert(sps.vui.low_delay_hrd_flag == 1);
    assert(sps.vui.pic_struct_present_flag == 1);
    assert(sps.vui.bitstream_restriction_flag == 1);
    assert(sps.vui.motion_vectors_over_pic_boundaries_flag == 1);
    assert(sps.vui.max_bytes_per_pic_denom == 2);
    assert(sps.vui.max_bits_per_mb_denom == 1);
    assert(sps.vui.log2_max_mv_length_horizontal == 16);
    assert(sps.vui.log2_max_mv_length_vertical == 15);
    assert(sps.vui.max_num_reorder_frames == 2);
    assert(sps.vui.max_dec_frame_buffering == 4);
}

static void write_pps_common_tail(bit_writer_t *bw)
{
    bw_put_ue(bw, 0);
    bw_put_ue(bw, 0);
    bw_put_bit(bw, 0);
    bw_put_bits(bw, 0, 2);
    bw_put_se(bw, 0);
    bw_put_se(bw, 0);
    bw_put_se(bw, 0);
    bw_put_bit(bw, 1);
    bw_put_bit(bw, 0);
    bw_put_bit(bw, 0);
}

static void test_pps_fmo_slice_groups(void)
{
    bit_writer_t bw;
    avc_pps_t pps;
    size_t size;

    bw = (bit_writer_t){0};
    bw_put_ue(&bw, 0);
    bw_put_ue(&bw, 0);
    bw_put_bit(&bw, 0);
    bw_put_bit(&bw, 0);
    bw_put_ue(&bw, 2);
    bw_put_ue(&bw, 6);
    bw_put_ue(&bw, 3);
    bw_put_bits(&bw, 0, 2);
    bw_put_bits(&bw, 1, 2);
    bw_put_bits(&bw, 2, 2);
    bw_put_bits(&bw, 1, 2);
    write_pps_common_tail(&bw);
    size = bw_finish_rbsp(&bw);

    assert(avc_parse_pps(bw.data, size, &pps));
    assert(pps.num_slice_groups_minus1 == 2);
    assert(pps.slice_group_map_type == 6);
    assert(pps.pic_size_in_map_units_minus1 == 3);
    assert(pps.slice_group_id_count == 4);
    assert(pps.slice_group_id[0] == 0);
    assert(pps.slice_group_id[1] == 1);
    assert(pps.slice_group_id[2] == 2);
    assert(pps.slice_group_id[3] == 1);
    assert(!pps.slice_group_id_truncated);

    bw = (bit_writer_t){0};
    bw_put_ue(&bw, 1);
    bw_put_ue(&bw, 0);
    bw_put_bit(&bw, 0);
    bw_put_bit(&bw, 0);
    bw_put_ue(&bw, 1);
    bw_put_ue(&bw, 0);
    bw_put_ue(&bw, 4);
    bw_put_ue(&bw, 7);
    write_pps_common_tail(&bw);
    size = bw_finish_rbsp(&bw);

    assert(avc_parse_pps(bw.data, size, &pps));
    assert(pps.num_slice_groups_minus1 == 1);
    assert(pps.slice_group_map_type == 0);
    assert(pps.run_length_minus1[0] == 4);
    assert(pps.run_length_minus1[1] == 7);
}

static void test_pps_scaling_lists(void)
{
    bit_writer_t bw;
    avc_parameter_sets_t sets;
    avc_pps_t pps;
    size_t size;
    unsigned i;

    sets = (avc_parameter_sets_t){0};
    sets.sps[0].present = 1;
    sets.sps[0].chroma_format_idc = 3;

    bw = (bit_writer_t){0};
    bw_put_ue(&bw, 2);
    bw_put_ue(&bw, 0);
    bw_put_bit(&bw, 0);
    bw_put_bit(&bw, 0);
    bw_put_ue(&bw, 0);
    write_pps_common_tail(&bw);
    bw_put_bit(&bw, 1);
    bw_put_bit(&bw, 1);
    bw_put_bit(&bw, 1);
    for (i = 0; i < 16; i++) {
        bw_put_se(&bw, 0);
    }
    bw_put_bit(&bw, 0);
    bw_put_bit(&bw, 0);
    bw_put_bit(&bw, 0);
    bw_put_bit(&bw, 0);
    bw_put_bit(&bw, 0);
    bw_put_bit(&bw, 1);
    bw_put_se(&bw, -8);
    bw_put_bit(&bw, 0);
    bw_put_bit(&bw, 0);
    bw_put_bit(&bw, 0);
    bw_put_bit(&bw, 0);
    bw_put_bit(&bw, 0);
    bw_put_se(&bw, -3);
    size = bw_finish_rbsp(&bw);

    assert(avc_parse_pps_with_sets(bw.data, size, &sets, &pps));
    assert(pps.transform_8x8_mode_flag == 1);
    assert(pps.pic_scaling_matrix_present_flag == 1);
    assert(pps.pic_scaling_list_present_flag[0] == 1);
    assert(pps.pic_scaling_list_present_flag[1] == 0);
    assert(pps.pic_scaling_list_present_flag[6] == 1);
    assert(pps.use_default_scaling_matrix_flag[6] == 1);
    assert(pps.scaling_list_4x4[0][0] == 8);
    assert(pps.scaling_list_4x4[1][0] == 8);
    assert(pps.scaling_list_8x8[0][0] == 6);
    assert(pps.scaling_list_8x8[0][63] == 42);
    assert(pps.scaling_list_8x8[1][0] == 9);
    assert(pps.scaling_list_8x8[1][63] == 35);
    assert(pps.second_chroma_qp_index_offset == -3);
}

int main(void)
{
    test_sps_scaling_matrices();
    test_sps_high_profile_edge_fields();
    test_sps_high_profile_validation();
    test_sps_vui_hrd();
    test_pps_fmo_slice_groups();
    test_pps_scaling_lists();
    return 0;
}
