#include "avc/avc_bitreader.h"
#include "avc/avc_cabac.h"
#include "avc/avc_cavlc.h"
#include "avc/avc_macroblock.h"
#include "avc/avc_syntax.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
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

typedef struct {
    const char *bits;
    unsigned max_coeff;
    unsigned total_coeff;
    unsigned expected;
} total_zeros_case_t;

typedef struct {
    const char *bits;
    unsigned zeros_left;
    unsigned expected;
} run_before_case_t;

typedef struct {
    const char *bits;
    int nC;
    unsigned max_coeff;
    avc_cavlc_scan_t scan_mode;
    unsigned total_coeff;
    unsigned trailing_ones;
    unsigned total_zeros;
    int level[4];
    unsigned scan[4];
    unsigned x[4];
    unsigned y[4];
    unsigned run[4];
} residual_case_t;

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

static void assert_total_zeros_case(const total_zeros_case_t *tc)
{
    bit_writer_t bw = {{0}, 0};
    avc_bitreader_t br;
    unsigned value = 999u;

    bw_put_bits(&bw, tc->bits);
    avc_br_init(&br, bw.data, sizeof(bw.data));
    assert(avc_cavlc_read_total_zeros(&br, tc->max_coeff, tc->total_coeff, &value));
    if (value != tc->expected) {
        fprintf(stderr, "total_zeros bits=%s max=%u total=%u expected=%u got=%u\n",
                tc->bits, tc->max_coeff, tc->total_coeff, tc->expected, value);
    }
    assert(value == tc->expected);
}

static void assert_run_before_case(const run_before_case_t *tc)
{
    bit_writer_t bw = {{0}, 0};
    avc_bitreader_t br;
    unsigned value = 999u;

    bw_put_bits(&bw, tc->bits);
    avc_br_init(&br, bw.data, sizeof(bw.data));
    assert(avc_cavlc_read_run_before(&br, tc->zeros_left, &value));
    if (value != tc->expected) {
        fprintf(stderr, "run_before bits=%s zeros_left=%u expected=%u got=%u\n",
                tc->bits, tc->zeros_left, tc->expected, value);
    }
    assert(value == tc->expected);
}

static void assert_residual_case(const residual_case_t *tc)
{
    bit_writer_t bw = {{0}, 0};
    avc_bitreader_t br;
    avc_cavlc_block_t block;
    coeff_trace_t trace = {0};
    avc_cavlc_callbacks_t callbacks = {0};
    unsigned i;

    callbacks.on_coeff = record_coeff;
    bw_put_bits(&bw, tc->bits);
    avc_br_init(&br, bw.data, sizeof(bw.data));
    if (!avc_cavlc_read_residual_block(&br, tc->nC, tc->max_coeff, tc->scan_mode,
                                       &block, callbacks, &trace)) {
        fprintf(stderr, "residual bits=%s failed\n", tc->bits);
        assert(0);
    }
    assert(block.total_coeff == tc->total_coeff);
    assert(block.trailing_ones == tc->trailing_ones);
    if (block.total_zeros != tc->total_zeros) {
        fprintf(stderr, "residual bits=%s total_zeros expected=%u got=%u\n",
                tc->bits, tc->total_zeros, block.total_zeros);
    }
    assert(block.total_zeros == tc->total_zeros);
    assert(trace.count == tc->total_coeff);

    for (i = 0; i < tc->total_coeff; i++) {
        assert(block.coeff_level[i] == tc->level[i]);
        assert(block.coeff_scan[i] == tc->scan[i]);
        assert(block.coeff_x[i] == tc->x[i]);
        if (block.coeff_y[i] != tc->y[i]) {
            fprintf(stderr, "residual bits=%s coeff[%u] y expected=%u got=%u scan=%u x=%u\n",
                    tc->bits, i, tc->y[i], block.coeff_y[i], block.coeff_scan[i],
                    block.coeff_x[i]);
        }
        assert(block.coeff_y[i] == tc->y[i]);
        assert(block.run_before[i] == tc->run[i]);
        assert(trace.level[i] == tc->level[i]);
        assert(trace.scan[i] == tc->scan[i]);
        assert(trace.run[i] == tc->run[i]);
    }
}

int main(void)
{
    {
        const uint8_t table0_zero[] = {0x80};
        const uint8_t table1_zero[] = {0xc0};
        const uint8_t table2_zero[] = {0xf0};
        const uint8_t table3_zero[] = {0x0c};
        const uint8_t chroma420_zero[] = {0x40};
        const uint8_t chroma422_zero[] = {0x80};
        avc_bitreader_t br;
        unsigned total_coeff;
        unsigned trailing_ones;

        avc_br_init(&br, table0_zero, sizeof(table0_zero));
        assert(avc_cavlc_read_coeff_token(&br, 0, 16, &total_coeff, &trailing_ones));
        assert(total_coeff == 0);
        assert(trailing_ones == 0);

        avc_br_init(&br, table1_zero, sizeof(table1_zero));
        assert(avc_cavlc_read_coeff_token(&br, 2, 16, &total_coeff, &trailing_ones));
        assert(total_coeff == 0);
        assert(trailing_ones == 0);

        avc_br_init(&br, table2_zero, sizeof(table2_zero));
        assert(avc_cavlc_read_coeff_token(&br, 4, 16, &total_coeff, &trailing_ones));
        assert(total_coeff == 0);
        assert(trailing_ones == 0);

        avc_br_init(&br, table3_zero, sizeof(table3_zero));
        assert(avc_cavlc_read_coeff_token(&br, 8, 16, &total_coeff, &trailing_ones));
        assert(total_coeff == 0);
        assert(trailing_ones == 0);

        avc_br_init(&br, chroma420_zero, sizeof(chroma420_zero));
        assert(avc_cavlc_read_coeff_token(&br, 0, 4, &total_coeff, &trailing_ones));
        assert(total_coeff == 0);
        assert(trailing_ones == 0);

        avc_br_init(&br, chroma422_zero, sizeof(chroma422_zero));
        assert(avc_cavlc_read_coeff_token(&br, 0, 8, &total_coeff, &trailing_ones));
        assert(total_coeff == 0);
        assert(trailing_ones == 0);
    }

    {
        const uint8_t total_zeros_zero[] = {0x80};
        const uint8_t total_zeros_two[] = {0x40};
        const uint8_t run_zero[] = {0x80};
        const uint8_t run_one[] = {0x00};
        const uint8_t level_pos[] = {0x80};
        const uint8_t level_neg[] = {0x40};
        avc_bitreader_t br;
        bit_writer_t bw = {{0}, 0};
        unsigned value;
        int level;

        avc_br_init(&br, total_zeros_zero, sizeof(total_zeros_zero));
        assert(avc_cavlc_read_total_zeros(&br, 16, 1, &value));
        assert(value == 0);

        avc_br_init(&br, total_zeros_two, sizeof(total_zeros_two));
        assert(avc_cavlc_read_total_zeros(&br, 16, 1, &value));
        assert(value == 2);

        avc_br_init(&br, run_zero, sizeof(run_zero));
        assert(avc_cavlc_read_run_before(&br, 1, &value));
        assert(value == 0);

        avc_br_init(&br, run_one, sizeof(run_one));
        assert(avc_cavlc_read_run_before(&br, 1, &value));
        assert(value == 1);

        avc_br_init(&br, level_pos, sizeof(level_pos));
        assert(avc_cavlc_read_level(&br, 0, &level));
        assert(level == 1);

        avc_br_init(&br, level_neg, sizeof(level_neg));
        assert(avc_cavlc_read_level(&br, 0, &level));
        assert(level == -1);

        {
            const total_zeros_case_t cases[] = {
                {"1", 16, 1, 0},
                {"011", 16, 1, 1},
                {"010", 16, 1, 2},
                {"000000001", 16, 1, 15},
                {"101", 16, 2, 2},
                {"000000", 16, 2, 14},
                {"011", 16, 4, 8},
                {"0", 16, 15, 0},
                {"1", 16, 15, 1},
                {"1", 4, 1, 0},
                {"01", 4, 1, 1},
                {"001", 4, 1, 2},
                {"000", 4, 1, 3},
                {"00", 4, 2, 2},
                {"0", 4, 3, 1},
                {"0011", 8, 1, 4},
                {"00000", 8, 1, 7},
                {"100", 8, 2, 3},
                {"111", 8, 2, 6},
                {"01", 8, 5, 1},
                {"1", 8, 7, 1},
            };
            size_t i;

            for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
                assert_total_zeros_case(&cases[i]);
            }
        }

        {
            const run_before_case_t cases[] = {
                {"1", 0, 0},
                {"1", 1, 0},
                {"0", 1, 1},
                {"01", 2, 1},
                {"00", 2, 2},
                {"11", 3, 0},
                {"00", 3, 3},
                {"01", 4, 2},
                {"000", 4, 4},
                {"001", 5, 4},
                {"000", 5, 5},
                {"100", 6, 6},
                {"00000000001", 7, 14},
                {"00000000001", 15, 14},
            };
            size_t i;

            for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
                assert_run_before_case(&cases[i]);
            }
        }

        memset(&bw, 0, sizeof(bw));
        bw_put_bits(&bw, "010");
        avc_br_init(&br, bw.data, sizeof(bw.data));
        assert(avc_cavlc_read_level(&br, 1, &level));
        assert(level == 2);

        memset(&bw, 0, sizeof(bw));
        bw_put_bits(&bw, "011");
        avc_br_init(&br, bw.data, sizeof(bw.data));
        assert(avc_cavlc_read_level(&br, 1, &level));
        assert(level == -2);

        memset(&bw, 0, sizeof(bw));
        bw_put_bits(&bw, "00111");
        avc_br_init(&br, bw.data, sizeof(bw.data));
        assert(avc_cavlc_read_level(&br, 2, &level));
        assert(level == -6);

        memset(&bw, 0, sizeof(bw));
        bw_put_bits(&bw, "0000000000000010000");
        avc_br_init(&br, bw.data, sizeof(bw.data));
        assert(avc_cavlc_read_level(&br, 0, &level));
        assert(level == 8);

        memset(&bw, 0, sizeof(bw));
        bw_put_bits(&bw, "0000000000000001000000000000");
        avc_br_init(&br, bw.data, sizeof(bw.data));
        assert(avc_cavlc_read_level(&br, 0, &level));
        assert(level == 16);

        assert(avc_cavlc_derive_nC_from_neighbors(0, 7, 0, 9) == 0);
        assert(avc_cavlc_derive_nC_from_neighbors(1, 7, 0, 9) == 7);
        assert(avc_cavlc_derive_nC_from_neighbors(0, 7, 1, 9) == 9);
        assert(avc_cavlc_derive_nC_from_neighbors(1, 7, 1, 8) == 8);
        assert(avc_cavlc_derive_nC_from_neighbors(1, 7, 1, 9) == 8);
    }

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
        const uint8_t neg_pos[] = {0x77};
        const uint8_t pos_neg[] = {0x6f};
        const uint8_t neg_neg[] = {0x7f};
        avc_bitreader_t br;
        avc_cavlc_block_t block;
        avc_cavlc_callbacks_t callbacks = {0};

        avc_br_init(&br, neg_pos, sizeof(neg_pos));
        assert(avc_cavlc_read_residual_block(&br, 2, 16, AVC_CAVLC_SCAN_FRAME, &block, callbacks, NULL));
        assert(block.total_coeff == 2);
        assert(block.trailing_ones == 2);
        assert(block.coeff_level[0] == -1);
        assert(block.coeff_level[1] == 1);

        avc_br_init(&br, pos_neg, sizeof(pos_neg));
        assert(avc_cavlc_read_residual_block(&br, 2, 16, AVC_CAVLC_SCAN_FRAME, &block, callbacks, NULL));
        assert(block.total_coeff == 2);
        assert(block.trailing_ones == 2);
        assert(block.coeff_level[0] == 1);
        assert(block.coeff_level[1] == -1);

        avc_br_init(&br, neg_neg, sizeof(neg_neg));
        assert(avc_cavlc_read_residual_block(&br, 2, 16, AVC_CAVLC_SCAN_FRAME, &block, callbacks, NULL));
        assert(block.total_coeff == 2);
        assert(block.trailing_ones == 2);
        assert(block.coeff_level[0] == -1);
        assert(block.coeff_level[1] == -1);
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
        const residual_case_t cases[] = {
            {
                "0110010101",
                2,
                16,
                AVC_CAVLC_SCAN_FRAME,
                2,
                2,
                2,
                {1, 1, 0, 0},
                {3, 1, 0, 0},
                {0, 1, 0, 0},
                {2, 0, 0, 0},
                {1, 1, 0, 0}
            },
            {
                "0110010101",
                2,
                16,
                AVC_CAVLC_SCAN_FIELD,
                2,
                2,
                2,
                {1, 1, 0, 0},
                {3, 1, 0, 0},
                {0, 0, 0, 0},
                {2, 1, 0, 0},
                {1, 1, 0, 0}
            },
            {
                "00101010",
                0,
                4,
                AVC_CAVLC_SCAN_FRAME,
                2,
                2,
                1,
                {1, -1, 0, 0},
                {2, 0, 0, 0},
                {0, 0, 0, 0},
                {1, 0, 0, 0},
                {1, 0, 0, 0}
            },
            {
                "001010100101101001010",
                2,
                16,
                AVC_CAVLC_SCAN_FRAME,
                3,
                1,
                4,
                {-1, 2, -2, 0},
                {6, 4, 2, 0},
                {3, 1, 0, 0},
                {0, 1, 1, 0},
                {1, 1, 2, 0}
            },
        };
        size_t i;

        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            assert_residual_case(&cases[i]);
        }
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

        assert(avc_cabac_ctx_mb_qp_delta(0) == 60);
        assert(avc_cabac_ctx_mb_qp_delta(1) == 61);
        assert(avc_cabac_ctx_transform_size_8x8(0, 0) == 0);
        assert(avc_cabac_ctx_transform_size_8x8(1, 0) == 1);
        assert(avc_cabac_ctx_transform_size_8x8(0, 1) == 1);
        assert(avc_cabac_ctx_transform_size_8x8(1, 1) == 2);
        assert(avc_cabac_ctx_mb_field_decoding_flag(0, 1, 0, 1) == 0);
        assert(avc_cabac_ctx_mb_field_decoding_flag(1, 1, 0, 1) == 1);
        assert(avc_cabac_ctx_mb_field_decoding_flag(0, 1, 1, 1) == 1);
        assert(avc_cabac_ctx_mb_field_decoding_flag(1, 1, 1, 1) == 2);

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

        assert(avc_cabac_ctx_coded_block_pattern_luma(0, 0, 0, 0, 0, 0) == 3);
        assert(avc_cabac_ctx_coded_block_pattern_luma(1, 0, 0, 0, 0, 1) == 2);
        assert(avc_cabac_ctx_coded_block_pattern_luma(2, 0, 0, 0, 0, 3) == 1);
        assert(avc_cabac_ctx_coded_block_pattern_luma(3, 0, 0, 0, 0, 7) == 0);
        assert(avc_cabac_ctx_coded_block_pattern_chroma(0, 0, 0, 0, 0) == 3);
        assert(avc_cabac_ctx_coded_block_pattern_chroma(0, 1, 1, 1, 1) == 0);
        assert(avc_cabac_ctx_coded_block_pattern_chroma(1, 0, 0, 0, 0) == 3);
        assert(avc_cabac_ctx_coded_block_pattern_chroma(1, 1, 2, 1, 2) == 0);

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
        const uint8_t data[] = {0x00, 0x00, 0x00, 0x00};
        avc_cabac_decoder_t cabac;
        avc_cabac_residual_block_t block;

        avc_cabac_init(&cabac, data, sizeof(data));
        assert(!cabac.error);
        assert(avc_cabac_init_contexts(&cabac, 26, 0, 0));
        assert(!avc_cabac_decode_residual_block(&cabac, 16, 0, 1, AVC_CABAC_CONTEXTS,
                                                105, 166, 227, 1, 1, 0, &block));
        assert(cabac.error);
        assert(block.error_syntax == AVC_CABAC_RESIDUAL_ERROR_CODED_BLOCK_FLAG);
        assert(block.error_context >= AVC_CABAC_CONTEXTS);
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

        assert(avc_cabac_ctx_ref_idx(0, 0) == 0);
        assert(avc_cabac_ctx_ref_idx(1, 0) == 1);
        assert(avc_cabac_ctx_ref_idx(0, 1) == 2);
        assert(avc_cabac_ctx_ref_idx(1, 1) == 3);
        assert(avc_cabac_ctx_mvd(0, 0) == 0);
        assert(avc_cabac_ctx_mvd(2, 0) == 0);
        assert(avc_cabac_ctx_mvd(2, 1) == 1);
        assert(avc_cabac_ctx_mvd(33, 0) == 2);
        assert(avc_cabac_ctx_coded_block_flag(0, 0) == 0);
        assert(avc_cabac_ctx_coded_block_flag(1, 0) == 1);
        assert(avc_cabac_ctx_coded_block_flag(0, 1) == 2);
        assert(avc_cabac_ctx_coded_block_flag(1, 1) == 3);
        assert(avc_cabac_ctx_residual_flag(0, 5, 16, 0, 0) == 5);
        assert(avc_cabac_ctx_residual_flag(3, 0, 4, 0, 0) == 0);
        assert(avc_cabac_ctx_residual_flag(3, 8, 16, 0, 0) == 2);
        assert(avc_cabac_ctx_residual_flag(5, 22, 64, 0, 0) == 6);
        assert(avc_cabac_ctx_residual_flag(5, 22, 64, 1, 0) == 12);
        assert(avc_cabac_ctx_residual_flag(5, 62, 64, 0, 1) == 8);

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
