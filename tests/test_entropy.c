#include "avc/avc_bitreader.h"
#include "avc/avc_cabac.h"
#include "avc/avc_cavlc.h"
#include "avc/avc_macroblock.h"
#include "avc/avc_syntax.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    unsigned count;
    unsigned scan[8];
    int level[8];
    unsigned run[8];
} coeff_trace_t;

typedef struct {
    uint8_t data[32];
    size_t bit_pos;
} bit_writer_t;

typedef struct {
    unsigned count;
    avc_residual_kind_t kind[32];
    unsigned block_index[32];
    unsigned total_coeff[32];
    unsigned component[32];
    unsigned chroma_format_idc[32];
    unsigned bit_depth_luma[32];
    unsigned bit_depth_chroma[32];
    unsigned scaling_list_size[32];
    int scaling_list_index[32];
} residual_trace_t;

static void bw_put_bit(bit_writer_t *bw, unsigned bit)
{
    assert(bw->bit_pos < sizeof(bw->data) * 8u);
    if (bit) {
        bw->data[bw->bit_pos >> 3] |= (uint8_t)(1u << (7u - (bw->bit_pos & 7u)));
    }
    bw->bit_pos++;
}

static void bw_put_bits(bit_writer_t *bw, const char *bits)
{
    while (*bits) {
        bw_put_bit(bw, *bits == '1');
        bits++;
    }
}

static void bw_put_ue(bit_writer_t *bw, uint32_t value)
{
    uint32_t code_num = value + 1u;
    unsigned bits = 0;
    uint32_t tmp = code_num;
    unsigned i;

    while (tmp) {
        bits++;
        tmp >>= 1;
    }
    for (i = 0; i + 1u < bits; i++) {
        bw_put_bit(bw, 0);
    }
    for (i = bits; i > 0; i--) {
        bw_put_bit(bw, (code_num >> (i - 1u)) & 1u);
    }
}

static void bw_put_se(bit_writer_t *bw, int32_t value)
{
    uint32_t code_num = value <= 0 ? (uint32_t)(-value * 2) : (uint32_t)(value * 2 - 1);
    bw_put_ue(bw, code_num);
}

static void record_coeff(void *opaque, unsigned scan_index, int level, unsigned run_before)
{
    coeff_trace_t *trace = (coeff_trace_t *)opaque;

    assert(trace->count < 8);
    trace->scan[trace->count] = scan_index;
    trace->level[trace->count] = level;
    trace->run[trace->count] = run_before;
    trace->count++;
}

static void record_residual(void *opaque, const avc_residual_event_t *residual)
{
    residual_trace_t *trace = (residual_trace_t *)opaque;

    assert(trace->count < 32);
    trace->kind[trace->count] = residual->block_kind;
    trace->block_index[trace->count] = residual->block_index;
    trace->total_coeff[trace->count] = residual->block.total_coeff;
    trace->component[trace->count] = residual->component;
    trace->chroma_format_idc[trace->count] = residual->chroma_format_idc;
    trace->bit_depth_luma[trace->count] = residual->bit_depth_luma;
    trace->bit_depth_chroma[trace->count] = residual->bit_depth_chroma;
    trace->scaling_list_size[trace->count] = residual->scaling_list_size;
    trace->scaling_list_index[trace->count] = residual->scaling_list_index;
    trace->count++;
}

int main(void)
{
    {
        unsigned x;
        unsigned y;

        assert(avc_cavlc_scan_position(16, 3, AVC_CAVLC_SCAN_FIELD, &x, &y));
        assert(x == 0);
        assert(y == 2);
        assert(avc_cavlc_scan_position(64, 3, AVC_CAVLC_SCAN_FIELD, &x, &y));
        assert(x == 1);
        assert(y == 0);
        assert(avc_cavlc_scan_position(16, 3, AVC_CAVLC_SCAN_TRANSFORM_BYPASS_FIELD, &x, &y));
        assert(x == 0);
        assert(y == 2);
    }

    {
        const uint8_t data[] = {0x80};
        avc_bitreader_t br;
        avc_cavlc_block_t block;
        avc_cavlc_callbacks_t callbacks = {0};

        avc_br_init(&br, data, sizeof(data));
        assert(avc_cavlc_read_residual_block(&br, 0, 16, AVC_CAVLC_SCAN_FRAME, &block, callbacks, NULL));
        assert(block.total_coeff == 0);
        assert(block.trailing_ones == 0);
    }

    {
        const uint8_t data[] = {0x50};
        avc_bitreader_t br;
        avc_cavlc_block_t block;
        avc_cavlc_callbacks_t callbacks = {0};

        avc_br_init(&br, data, sizeof(data));
        assert(avc_cavlc_read_residual_block(&br, 0, 16, AVC_CAVLC_SCAN_FRAME, &block, callbacks, NULL));
        assert(block.total_coeff == 1);
        assert(block.trailing_ones == 1);
        assert(block.coeff_level[0] == 1);
        assert(block.coeff_scan[0] == 0);
        assert(block.coeff_x[0] == 0);
        assert(block.coeff_y[0] == 0);
        assert(block.total_zeros == 0);
    }

    {
        const uint8_t data[] = {0xc0};
        avc_bitreader_t br;
        avc_cavlc_block_t block;
        avc_cavlc_callbacks_t callbacks = {0};

        avc_br_init(&br, data, sizeof(data));
        assert(avc_cavlc_read_residual_block(&br, 2, 16, AVC_CAVLC_SCAN_FRAME, &block, callbacks, NULL));
        assert(block.total_coeff == 0);
        assert(block.trailing_ones == 0);
    }

    {
        const uint8_t data[] = {0x50};
        avc_bitreader_t br;
        avc_cavlc_block_t block;
        avc_cavlc_callbacks_t callbacks = {0};

        avc_br_init(&br, data, sizeof(data));
        assert(avc_cavlc_read_residual_block(&br, 0, 15, AVC_CAVLC_SCAN_FRAME, &block, callbacks, NULL));
        assert(block.total_coeff == 1);
        assert(block.coeff_scan[0] == 0);
        assert(block.coeff_x[0] == 1);
        assert(block.coeff_y[0] == 0);
    }

    {
        const uint8_t data[] = {0xf0};
        avc_bitreader_t br;
        avc_cavlc_block_t block;
        avc_cavlc_callbacks_t callbacks = {0};

        avc_br_init(&br, data, sizeof(data));
        assert(avc_cavlc_read_residual_block(&br, 4, 16, AVC_CAVLC_SCAN_FRAME, &block, callbacks, NULL));
        assert(block.total_coeff == 0);
        assert(block.trailing_ones == 0);
    }

    {
        const uint8_t data[] = {0x0c};
        avc_bitreader_t br;
        avc_cavlc_block_t block;
        avc_cavlc_callbacks_t callbacks = {0};

        avc_br_init(&br, data, sizeof(data));
        assert(avc_cavlc_read_residual_block(&br, 8, 16, AVC_CAVLC_SCAN_FRAME, &block, callbacks, NULL));
        assert(block.total_coeff == 0);
        assert(block.trailing_ones == 0);
    }

    {
        const uint8_t data[] = {0x40};
        avc_bitreader_t br;
        avc_cavlc_block_t block;
        avc_cavlc_callbacks_t callbacks = {0};

        avc_br_init(&br, data, sizeof(data));
        assert(avc_cavlc_read_residual_block(&br, 0, 4, AVC_CAVLC_SCAN_FRAME, &block, callbacks, NULL));
        assert(block.total_coeff == 0);
        assert(block.trailing_ones == 0);
    }

    {
        const uint8_t data[] = {0x67};
        avc_bitreader_t br;
        avc_cavlc_block_t block;
        avc_cavlc_callbacks_t callbacks = {0};

        avc_br_init(&br, data, sizeof(data));
        assert(avc_cavlc_read_residual_block(&br, 2, 16, AVC_CAVLC_SCAN_FRAME, &block, callbacks, NULL));
        assert(block.total_coeff == 2);
        assert(block.trailing_ones == 2);
        assert(block.coeff_level[0] == 1);
        assert(block.coeff_level[1] == 1);
        assert(block.coeff_scan[0] == 1);
        assert(block.coeff_scan[1] == 0);
        assert(block.coeff_x[0] == 1);
        assert(block.coeff_y[0] == 0);
        assert(block.coeff_x[1] == 0);
        assert(block.coeff_y[1] == 0);
        assert(block.total_zeros == 0);
    }

    {
        const uint8_t data[] = {0x65, 0x40};
        avc_bitreader_t br;
        avc_cavlc_block_t block;
        coeff_trace_t trace = {0};
        avc_cavlc_callbacks_t callbacks = {0};

        callbacks.on_coeff = record_coeff;
        avc_br_init(&br, data, sizeof(data));
        assert(avc_cavlc_read_residual_block(&br, 2, 16, AVC_CAVLC_SCAN_FRAME, &block, callbacks, &trace));
        assert(block.total_coeff == 2);
        assert(block.trailing_ones == 2);
        assert(block.total_zeros == 2);
        assert(block.run_before[0] == 1);
        assert(block.run_before[1] == 1);
        assert(block.coeff_scan[0] == 3);
        assert(block.coeff_scan[1] == 1);
        assert(block.coeff_x[0] == 0);
        assert(block.coeff_y[0] == 2);
        assert(block.coeff_x[1] == 1);
        assert(block.coeff_y[1] == 0);
        assert(trace.count == 2);
        assert(trace.scan[0] == 3);
        assert(trace.scan[1] == 1);
        assert(trace.level[0] == 1);
        assert(trace.level[1] == 1);
        assert(trace.run[0] == 1);
        assert(trace.run[1] == 1);
    }

    {
        const uint8_t data[] = {0x65, 0x40};
        avc_bitreader_t br;
        avc_cavlc_block_t block;
        avc_cavlc_callbacks_t callbacks = {0};

        avc_br_init(&br, data, sizeof(data));
        assert(avc_cavlc_read_residual_block(&br, 2, 16, AVC_CAVLC_SCAN_FIELD, &block, callbacks, NULL));
        assert(block.total_coeff == 2);
        assert(block.coeff_scan[0] == 3);
        assert(block.coeff_scan[1] == 1);
        assert(block.coeff_x[0] == 0);
        assert(block.coeff_y[0] == 2);
        assert(block.coeff_x[1] == 0);
        assert(block.coeff_y[1] == 1);
    }

    {
        avc_parameter_sets_t sets;
        avc_slice_header_t slice;
        avc_slice_data_summary_t summary;
        avc_macroblock_callbacks_t callbacks = {0};
        residual_trace_t trace = {0};
        bit_writer_t bw = {{0}, 0};

        memset(&sets, 0, sizeof(sets));
        sets.sps[0].present = 1;
        sets.sps[0].chroma_format_idc = 1;
        sets.sps[0].bit_depth_luma_minus8 = 2;
        sets.sps[0].bit_depth_chroma_minus8 = 2;
        sets.sps[0].frame_mbs_only_flag = 1;
        sets.pps[0].present = 1;
        sets.pps[0].transform_8x8_mode_flag = 1;

        slice = (avc_slice_header_t){0};
        slice.valid = 1;
        slice.slice_kind = AVC_SLICE_I;

        callbacks.on_residual = record_residual;
        bw_put_ue(&bw, 0);
        bw_put_bit(&bw, 1);
        bw_put_bits(&bw, "1111");
        bw_put_ue(&bw, 0);
        bw_put_ue(&bw, 30);
        bw_put_se(&bw, 0);
        bw_put_bits(&bw, "00100111100111");

        assert(avc_parse_slice_data(bw.data, (bw.bit_pos + 7u) >> 3,
                                    &slice, &sets, callbacks, &trace, &summary));
        assert(summary.macroblocks_seen == 1);
        assert(trace.count == 4);
        assert(trace.kind[0] == AVC_RESIDUAL_LUMA_8X8);
        assert(trace.block_index[0] == 4);
        assert(trace.total_coeff[0] == 2);
        assert(trace.component[0] == 0);
        assert(trace.chroma_format_idc[0] == 1);
        assert(trace.bit_depth_luma[0] == 10);
        assert(trace.bit_depth_chroma[0] == 10);
        assert(trace.scaling_list_size[0] == 8);
        assert(trace.scaling_list_index[0] == 0);
        assert(trace.block_index[1] == 5);
        assert(trace.total_coeff[1] == 1);
        assert(trace.block_index[2] == 6);
        assert(trace.total_coeff[2] == 0);
        assert(trace.block_index[3] == 7);
        assert(trace.total_coeff[3] == 0);
    }

    {
        const uint8_t data[] = {0x12, 0x34, 0x56, 0x78, 0x9a};
        avc_cabac_decoder_t cabac;
        unsigned value;
        int delta;

        avc_cabac_init(&cabac, data, sizeof(data));
        assert(!cabac.error);
        assert(avc_cabac_init_contexts(&cabac, 26, 0, AVC_SLICE_P));
        avc_cabac_set_context(&cabac, 14, 20, 0);
        assert(avc_cabac_decode_mb_type_p(&cabac, &value));
        assert(value <= 31);
        assert(avc_cabac_decode_mb_qp_delta(&cabac, 0, &delta));
        assert(delta > -128 && delta < 128);
        assert(!cabac.error);
    }

    {
        const uint8_t data[] = {0xff, 0x00};
        avc_cabac_decoder_t cabac;
        unsigned value;

        avc_cabac_init(&cabac, data, sizeof(data));
        assert(!cabac.error);
        assert(avc_cabac_init_contexts(&cabac, 26, 0, AVC_SLICE_P));
        assert(cabac.ctx[14].state == 53);
        assert(cabac.ctx[14].mps == 0);
        assert(cabac.ctx[105].state != 10 || cabac.ctx[105].mps != 0);
        assert(avc_cabac_init_contexts(&cabac, 26, 0, AVC_SLICE_I));
        assert(cabac.ctx[14].state == 62);
        assert(cabac.ctx[14].mps == 0);
        assert(avc_cabac_init_contexts(&cabac, 26, 1, AVC_SLICE_P));
        assert(cabac.ctx[14].state == 58);
        assert(cabac.ctx[14].mps == 0);
        assert(avc_cabac_init_contexts(&cabac, 26, 2, AVC_SLICE_B));
        assert(cabac.ctx[14].state == 29);
        assert(cabac.ctx[14].mps == 0);
        avc_cabac_set_context(&cabac, 231, 63, 0);
        assert(avc_cabac_decode_coeff_abs_level_minus1_stateful(&cabac, 227, 0, 2, 0, &value));
        assert(value < 128);
    }

    {
        const uint8_t data[] = {0x7f, 0xff, 0x00, 0xaa, 0x55, 0x33};
        avc_cabac_decoder_t cabac;
        unsigned cbp_luma;
        unsigned cbp_chroma;
        unsigned mode;

        avc_cabac_init(&cabac, data, sizeof(data));
        assert(!cabac.error);
        assert(avc_cabac_init_contexts(&cabac, 26, 0, 0));
        assert(avc_cabac_decode_intra_chroma_pred_mode(&cabac, &mode));
        assert(mode <= 3);
        assert(avc_cabac_decode_coded_block_pattern_luma(&cabac, 0, 0, 0, 0, &cbp_luma));
        assert(cbp_luma < 16);
        assert(avc_cabac_decode_coded_block_pattern_chroma(&cabac, 0, 0, 0, 0, &cbp_chroma));
        assert(cbp_chroma <= 2);
        assert(!cabac.error);
    }

    {
        const uint8_t data[] = {0x80, 0x00, 0x00, 0x00};
        avc_cabac_decoder_t cabac;
        avc_cabac_residual_block_t block;

        avc_cabac_init(&cabac, data, sizeof(data));
        assert(!cabac.error);
        assert(avc_cabac_init_contexts(&cabac, 26, 0, 0));
        assert(avc_cabac_decode_residual_block(&cabac, 16, 0, 1, 85, 105, 166, 227, 1, 1, 0, &block));
        assert(block.max_coeff == 16);
        assert(block.total_coeff <= 16);
        assert(!cabac.error);
    }

    {
        const uint8_t data[] = {0x00, 0x00, 0x55, 0xaa};
        avc_cabac_decoder_t cabac;
        uint32_t sample;

        avc_cabac_init(&cabac, data, sizeof(data));
        assert(!cabac.error);
        avc_cabac_byte_align(&cabac);
        assert(!cabac.error);
        sample = avc_cabac_read_pcm_bits(&cabac, 8);
        assert(sample == 0x55);
        sample = avc_cabac_read_pcm_bits(&cabac, 8);
        assert(sample == 0xaa);
        assert(!cabac.error);
    }

    {
        const uint8_t data[] = {0x33, 0xcc, 0x55, 0xaa, 0x0f, 0xf0};
        avc_cabac_decoder_t cabac;
        unsigned sub_type;
        unsigned ref_idx;
        int16_t mvd;

        avc_cabac_init(&cabac, data, sizeof(data));
        assert(!cabac.error);
        assert(avc_cabac_init_contexts(&cabac, 26, 0, 0));
        assert(avc_cabac_decode_sub_mb_type_p(&cabac, &sub_type));
        assert(sub_type <= 4);
        assert(avc_cabac_decode_sub_mb_type_b(&cabac, &sub_type));
        assert(sub_type <= 13);
        assert(avc_cabac_decode_ref_idx_l0(&cabac, 0, 0, &ref_idx));
        assert(ref_idx < 32);
        assert(avc_cabac_decode_mvd_component(&cabac, 40, 0, 0, &mvd));
        assert(mvd > -32768);
        assert(!cabac.error);
    }

    return 0;
}
