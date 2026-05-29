#include "avc/avc_bitreader.h"
#include "avc/avc_cabac.h"
#include "avc/avc_cavlc.h"
#include <assert.h>
#include <stdint.h>

typedef struct {
    unsigned count;
    unsigned scan[8];
    int level[8];
    unsigned run[8];
} coeff_trace_t;

static void record_coeff(void *opaque, unsigned scan_index, int level, unsigned run_before)
{
    coeff_trace_t *trace = (coeff_trace_t *)opaque;

    assert(trace->count < 8);
    trace->scan[trace->count] = scan_index;
    trace->level[trace->count] = level;
    trace->run[trace->count] = run_before;
    trace->count++;
}

int main(void)
{
    {
        const uint8_t data[] = {0x80};
        avc_bitreader_t br;
        avc_cavlc_block_t block;
        avc_cavlc_callbacks_t callbacks = {0};

        avc_br_init(&br, data, sizeof(data));
        assert(avc_cavlc_read_residual_block(&br, 0, 16, &block, callbacks, NULL));
        assert(block.total_coeff == 0);
        assert(block.trailing_ones == 0);
    }

    {
        const uint8_t data[] = {0x50};
        avc_bitreader_t br;
        avc_cavlc_block_t block;
        avc_cavlc_callbacks_t callbacks = {0};

        avc_br_init(&br, data, sizeof(data));
        assert(avc_cavlc_read_residual_block(&br, 0, 16, &block, callbacks, NULL));
        assert(block.total_coeff == 1);
        assert(block.trailing_ones == 1);
        assert(block.coeff_level[0] == 1);
        assert(block.total_zeros == 0);
    }

    {
        const uint8_t data[] = {0xc0};
        avc_bitreader_t br;
        avc_cavlc_block_t block;
        avc_cavlc_callbacks_t callbacks = {0};

        avc_br_init(&br, data, sizeof(data));
        assert(avc_cavlc_read_residual_block(&br, 2, 16, &block, callbacks, NULL));
        assert(block.total_coeff == 0);
        assert(block.trailing_ones == 0);
    }

    {
        const uint8_t data[] = {0xf0};
        avc_bitreader_t br;
        avc_cavlc_block_t block;
        avc_cavlc_callbacks_t callbacks = {0};

        avc_br_init(&br, data, sizeof(data));
        assert(avc_cavlc_read_residual_block(&br, 4, 16, &block, callbacks, NULL));
        assert(block.total_coeff == 0);
        assert(block.trailing_ones == 0);
    }

    {
        const uint8_t data[] = {0x0c};
        avc_bitreader_t br;
        avc_cavlc_block_t block;
        avc_cavlc_callbacks_t callbacks = {0};

        avc_br_init(&br, data, sizeof(data));
        assert(avc_cavlc_read_residual_block(&br, 8, 16, &block, callbacks, NULL));
        assert(block.total_coeff == 0);
        assert(block.trailing_ones == 0);
    }

    {
        const uint8_t data[] = {0x40};
        avc_bitreader_t br;
        avc_cavlc_block_t block;
        avc_cavlc_callbacks_t callbacks = {0};

        avc_br_init(&br, data, sizeof(data));
        assert(avc_cavlc_read_residual_block(&br, 0, 4, &block, callbacks, NULL));
        assert(block.total_coeff == 0);
        assert(block.trailing_ones == 0);
    }

    {
        const uint8_t data[] = {0x67};
        avc_bitreader_t br;
        avc_cavlc_block_t block;
        avc_cavlc_callbacks_t callbacks = {0};

        avc_br_init(&br, data, sizeof(data));
        assert(avc_cavlc_read_residual_block(&br, 2, 16, &block, callbacks, NULL));
        assert(block.total_coeff == 2);
        assert(block.trailing_ones == 2);
        assert(block.coeff_level[0] == 1);
        assert(block.coeff_level[1] == 1);
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
        assert(avc_cavlc_read_residual_block(&br, 2, 16, &block, callbacks, &trace));
        assert(block.total_coeff == 2);
        assert(block.trailing_ones == 2);
        assert(block.total_zeros == 2);
        assert(block.run_before[0] == 1);
        assert(block.run_before[1] == 1);
        assert(trace.count == 2);
        assert(trace.scan[0] == 3);
        assert(trace.scan[1] == 1);
        assert(trace.level[0] == 1);
        assert(trace.level[1] == 1);
        assert(trace.run[0] == 1);
        assert(trace.run[1] == 1);
    }

    {
        const uint8_t data[] = {0x12, 0x34, 0x56, 0x78, 0x9a};
        avc_cabac_decoder_t cabac;
        unsigned value;
        int delta;

        avc_cabac_init(&cabac, data, sizeof(data));
        assert(!cabac.error);
        assert(avc_cabac_init_contexts(&cabac, 26, 0, 2));
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
        assert(avc_cabac_init_contexts(&cabac, 26, 0, 2));
        assert(cabac.ctx[14].state == 53);
        assert(cabac.ctx[14].mps == 0);
        assert(cabac.ctx[105].state != 10 || cabac.ctx[105].mps != 0);
        avc_cabac_set_context(&cabac, 231, 63, 0);
        assert(avc_cabac_decode_coeff_abs_level_minus1_stateful(&cabac, 227, 2, 0, &value));
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
        assert(avc_cabac_decode_residual_block(&cabac, 16, 85, 105, 166, 227, 1, 1, &block));
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
