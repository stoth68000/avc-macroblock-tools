#include "avc/avc_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct {
    int filter_mb;
    uint32_t mb_address;
} probe_options_t;

static int want_mb(const probe_options_t *options, uint32_t mb_address)
{
    return !options->filter_mb || options->mb_address == mb_address;
}

static void on_nal(void *opaque, const avc_nal_unit_t *nal)
{
    (void)opaque;
    printf("{\"event\":\"nal\",\"offset\":%zu,\"type\":%u,\"type_name\":\"%s\",\"nal_ref_idc\":%u,\"ebsp_size\":%zu}\n",
           nal->offset, nal->header.nal_unit_type, avc_nal_type_name(nal->header.nal_unit_type),
           nal->header.nal_ref_idc, nal->ebsp_size);
}

static void on_sps(void *opaque, const avc_sps_t *sps)
{
    (void)opaque;
    printf("{\"event\":\"sps\",\"id\":%u,\"profile_idc\":%u,\"level_idc\":%u,\"width\":%u,\"height\":%u,\"max_num_ref_frames\":%u,\"frame_mbs_only\":%u,\"mbaff\":%u,\"vui_present\":%u}\n",
           sps->seq_parameter_set_id, sps->profile_idc, sps->level_idc,
           avc_sps_width(sps), avc_sps_height(sps), sps->max_num_ref_frames,
           sps->frame_mbs_only_flag, sps->mb_adaptive_frame_field_flag,
           sps->vui_parameters_present_flag);
}

static void on_pps(void *opaque, const avc_pps_t *pps)
{
    (void)opaque;
    printf("{\"event\":\"pps\",\"id\":%u,\"sps_id\":%u,\"cabac\":%u,\"slice_groups_minus1\":%u,\"deblocking_control\":%u,\"transform_8x8\":%u}\n",
           pps->pic_parameter_set_id, pps->seq_parameter_set_id,
           pps->entropy_coding_mode_flag, pps->num_slice_groups_minus1,
           pps->deblocking_filter_control_present_flag, pps->transform_8x8_mode_flag);
}

static void on_slice(void *opaque, const avc_slice_header_t *slice)
{
    (void)opaque;
    printf("{\"event\":\"slice\",\"nal_type\":%u,\"idr\":%s,\"slice_type\":\"%s\",\"first_mb\":%u,\"pps_id\":%u,\"frame_num\":%u,\"poc_lsb\":%u,\"field_pic\":%u,\"bottom_field\":%u,\"nal_ref_idc\":%u,\"header_bits\":%zu}\n",
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

static void on_ref_lists(void *opaque, const avc_slice_header_t *slice,
                         const avc_ref_list_state_t *lists)
{
    unsigned i;

    (void)opaque;
    printf("{\"event\":\"ref_lists\",\"slice_first_mb\":%u,\"list0\":[",
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
    printf("{\"event\":\"macroblock\",\"address\":%u,\"entropy\":\"%s\",\"skipped\":%s,\"mb_type\":%u,\"mb_skip_run\":%u,\"cbp_luma\":%u,\"cbp_chroma\":%u,\"transform_8x8\":%s,\"mb_qp_delta\":%d,\"pcm_luma_samples\":%u,\"pcm_chroma_samples\":%u,\"pcm_luma_bits\":%d,\"pcm_chroma_bits\":%d}\n",
           mb->address, mb->entropy == AVC_MB_ENTROPY_CABAC ? "cabac" : "cavlc",
           mb->skipped ? "true" : "false", mb->mb_type, mb->mb_skip_run,
           mb->coded_block_pattern_luma, mb->coded_block_pattern_chroma,
           mb->transform_size_8x8_flag ? "true" : "false", mb->mb_qp_delta,
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

static void on_mb_pred(void *opaque, const avc_mb_pred_event_t *pred)
{
    const probe_options_t *options = (const probe_options_t *)opaque;

    if (!want_mb(options, pred->mb_address)) {
        return;
    }
    printf("{\"event\":\"mb_pred\",\"mb_address\":%u,\"kind\":\"%s\",\"intra_chroma_pred_mode\":%u,\"partitions\":%u,\"direct_spatial\":%u,\"direct\":[%u,%u,%u,%u],\"list_mask\":[%u,%u,%u,%u],\"ref0\":[%u,%u,%u,%u],\"ref1\":[%u,%u,%u,%u],\"mvd0\":[[%d,%d],[%d,%d],[%d,%d],[%d,%d]],\"mvd1\":[[%d,%d],[%d,%d],[%d,%d],[%d,%d]],\"mv_pred0\":[[%d,%d],[%d,%d],[%d,%d],[%d,%d]],\"mv_pred1\":[[%d,%d],[%d,%d],[%d,%d],[%d,%d]],\"mv0\":[[%d,%d],[%d,%d],[%d,%d],[%d,%d]],\"mv1\":[[%d,%d],[%d,%d],[%d,%d],[%d,%d]],\"sub_mb_type\":[%u,%u,%u,%u]}\n",
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
           pred->sub_mb_type[2], pred->sub_mb_type[3]);
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
        printf("{\"event\":\"residual\",\"entropy\":\"cabac\",\"mb_address\":%u,\"block_kind\":\"%s\",\"block_index\":%u,\"total_coeff\":%u,\"coefficients\":[",
               residual->mb_address, residual_kind_name(residual->block_kind), residual->block_index,
               residual->cabac_block.total_coeff);
        for (i = 0; i < residual->cabac_block.max_coeff; i++) {
            if (residual->cabac_block.coeff_level[i] != 0) {
                printf("%s{\"scan\":%u,\"level\":%d}",
                       emitted == 0 ? "" : ",", i, residual->cabac_block.coeff_level[i]);
                emitted++;
            }
        }
        printf("]}\n");
    } else {
        unsigned scan = residual->block.total_zeros + residual->block.total_coeff;

        printf("{\"event\":\"residual\",\"entropy\":\"cavlc\",\"mb_address\":%u,\"block_kind\":\"%s\",\"block_index\":%u,\"total_coeff\":%u,\"trailing_ones\":%u,\"total_zeros\":%u,\"coefficients\":[",
               residual->mb_address, residual_kind_name(residual->block_kind), residual->block_index,
               residual->block.total_coeff, residual->block.trailing_ones,
               residual->block.total_zeros);
        for (i = 0; i < residual->block.total_coeff; i++) {
            if (scan > 0) {
                scan--;
            }
            printf("%s{\"coded_index\":%u,\"scan\":%u,\"level\":%d,\"run_before\":%u}",
                   i == 0 ? "" : ",", i, scan,
                   residual->block.coeff_level[i], residual->block.run_before[i]);
            if (i + 1u < residual->block.total_coeff) {
                if (scan > residual->block.run_before[i]) {
                    scan -= residual->block.run_before[i];
                } else {
                    scan = 0;
                }
            }
        }
        printf("]}\n");
    }
}

static void on_slice_data(void *opaque, const avc_slice_data_summary_t *summary)
{
    (void)opaque;
    printf("{\"event\":\"slice_data\",\"entropy\":\"%s\",\"macroblocks_seen\":%u,\"max_macroblocks\":%u,\"cabac_initialized\":%s,\"complete\":%s}\n",
           summary->entropy_coding_mode_flag ? "cabac" : "cavlc",
           summary->macroblocks_seen, summary->max_macroblocks,
           summary->cabac_initialized ? "true" : "false",
           summary->complete ? "true" : "false");
}

static void on_note(void *opaque, const char *message)
{
    (void)opaque;
    fprintf(stderr, "{\"event\":\"note\",\"message\":\"%s\"}\n", message);
}

static void on_error(void *opaque, const char *message, size_t offset)
{
    (void)opaque;
    fprintf(stderr, "{\"event\":\"error\",\"offset\":%zu,\"message\":\"%s\"}\n", offset, message);
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

int main(int argc, char **argv)
{
    avc_parser_t parser;
    avc_parser_callbacks_t callbacks = {0};
    probe_options_t options = {0};
    const char *input_path;
    uint8_t *data;
    size_t size = 0;
    int ok;

    if (argc == 2) {
        input_path = argv[1];
    } else if (argc == 4 && argv[1][0] == '-' && argv[1][1] == '-' &&
               argv[1][2] == 'm' && argv[1][3] == 'b' && argv[1][4] == '\0') {
        char *end = NULL;
        unsigned long value = strtoul(argv[2], &end, 10);
        if (!end || *end != '\0' || value > 0xffffffffUL) {
            fprintf(stderr, "invalid macroblock address: %s\n", argv[2]);
            return 2;
        }
        options.filter_mb = 1;
        options.mb_address = (uint32_t)value;
        input_path = argv[3];
    } else {
        fprintf(stderr, "usage: %s [--mb address] input.264\n", argv[0]);
        return 2;
    }

    data = read_file(input_path, &size);
    if (!data) {
        fprintf(stderr, "failed to read %s\n", input_path);
        return 1;
    }

    callbacks.on_nal = on_nal;
    callbacks.on_sps = on_sps;
    callbacks.on_pps = on_pps;
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
