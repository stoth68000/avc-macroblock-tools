#include "avc/avc_cabac.h"
#include <string.h>

typedef struct {
    unsigned ctx_idx;
    int8_t m;
    int8_t n;
} cabac_ctx_init_entry_t;

static const uint8_t range_lps[64][4] = {
    {128,176,208,240},{128,167,197,227},{128,158,187,216},{123,150,178,205},
    {116,142,169,195},{111,135,160,185},{105,128,152,175},{100,122,144,166},
    {95,116,137,158},{90,110,130,150},{85,104,123,142},{81,99,117,135},
    {77,94,111,128},{73,89,105,122},{69,85,100,116},{66,80,95,110},
    {62,76,90,104},{59,72,86,99},{56,69,81,94},{53,65,77,89},
    {51,62,73,85},{48,59,69,80},{46,56,66,76},{43,53,63,72},
    {41,50,59,69},{39,48,56,65},{37,45,54,62},{35,43,51,59},
    {33,41,48,56},{32,39,46,53},{30,37,43,50},{29,35,41,48},
    {27,33,39,45},{26,31,37,43},{24,30,35,41},{23,28,33,39},
    {22,27,32,37},{21,26,30,35},{20,24,29,33},{19,23,27,31},
    {18,22,26,30},{17,21,25,28},{16,20,23,27},{15,19,22,25},
    {14,18,21,24},{14,17,20,23},{13,16,19,22},{12,15,18,21},
    {12,14,17,20},{11,14,16,19},{11,13,15,18},{10,12,15,17},
    {10,12,14,16},{9,11,13,15},{9,11,12,14},{8,10,12,14},
    {8,9,11,13},{7,9,11,12},{7,9,10,12},{7,8,10,11},
    {6,8,9,11},{6,7,9,10},{6,7,8,9},{2,2,2,2}
};

static const uint8_t trans_idx_lps[64] = {
    0,0,1,2,2,4,4,5,6,7,8,9,9,11,11,12,
    13,13,15,15,16,16,18,18,19,19,21,21,22,22,23,24,
    24,25,26,26,27,27,28,29,29,30,30,30,31,32,32,33,
    33,33,34,34,35,35,35,36,36,36,37,37,37,38,38,63
};

static uint8_t clip_uint8(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return (uint8_t)min_value;
    }
    if (value > max_value) {
        return (uint8_t)max_value;
    }
    return (uint8_t)value;
}

static void cabac_init_context_from_mn(avc_cabac_context_t *ctx,
                                       unsigned slice_qp_y,
                                       int m,
                                       int n)
{
    int pre_ctx_state = clip_uint8(((m * (int)slice_qp_y) >> 4) + n, 1, 126);

    if (pre_ctx_state <= 63) {
        ctx->state = (uint8_t)(63 - pre_ctx_state);
        ctx->mps = 0;
    } else {
        ctx->state = (uint8_t)(pre_ctx_state - 64);
        ctx->mps = 1;
    }
}

static const cabac_ctx_init_entry_t cabac_init_common[] = {
    {3, 20, -15}, {4, 2, 54}, {5, 3, 74},
    {11, 23, 33}, {12, 23, 2}, {13, 21, 0},
    {14, 1, 9}, {15, 0, 49}, {16, -37, 118},
    {21, 12, 49}, {22, -4, 73}, {23, 17, 50},
    {24, 18, 64}, {25, 9, 43}, {26, 29, 0},
    {27, 26, 67}, {28, 16, 90}, {29, 9, 104},
    {36, -6, 86}, {37, -17, 95}, {38, -6, 61},
    {40, -3, 69}, {41, -6, 81}, {42, -11, 96},
    {47, -24, 102}, {48, -23, 97}, {49, -15, 84},
    {54, -3, 75}, {55, -1, 23}, {56, 1, 34},
    {58, 1, 54}, {60, 11, 51}, {61, 7, 67},
    {62, -5, 86}, {63, 2, 88}, {64, 0, 58},
    {65, -3, 76}, {66, -10, 94}, {67, 5, 54},
    {68, 4, 69}, {69, -3, 81},
    {73, 13, 41}, {74, 3, 62}, {75, -2, 79}, {76, 5, 59},
    {77, 6, 55}, {78, -5, 86}, {79, -3, 92}, {80, -10, 85},
    {81, -1, 66}, {82, -4, 77}, {83, -2, 71}, {84, -2, 75},
    {85, -10, 77},
    {105, -6, 93}, {106, -6, 84}, {107, -8, 79}, {108, 0, 66},
    {109, -1, 71}, {110, 0, 62}, {111, -2, 60}, {112, -2, 59},
    {113, -5, 75}, {114, -3, 62}, {115, -4, 58}, {116, -9, 66},
    {117, -1, 79}, {118, 0, 71}, {119, 3, 68}, {120, 10, 44},
    {166, -13, 106}, {167, -16, 106}, {168, -10, 87}, {169, -21, 114},
    {170, -18, 110}, {171, -14, 98}, {172, -22, 110}, {173, -21, 106},
    {174, -18, 103}, {175, -21, 107}, {176, -23, 108}, {177, -26, 112},
    {178, -10, 96}, {179, -12, 95}, {180, -5, 91}, {181, -9, 93},
    {227, -7, 92}, {228, -5, 89}, {229, -7, 96}, {230, -13, 108},
    {231, -3, 46}, {232, -1, 65}, {233, -1, 57}, {234, -9, 93},
    {235, -3, 74}, {236, -9, 92}, {237, -8, 87}, {238, -23, 126},
    {239, 5, 54}, {240, 6, 60}, {241, 6, 59}, {242, 6, 69},
    {399, 12, 40}, {400, 11, 51}, {401, 14, 59},
    {402, 0, 68}, {403, -2, 85}, {404, -6, 78}, {405, -1, 75},
    {406, -7, 77}, {407, 2, 54}, {408, 5, 50}, {409, -3, 68},
    {410, 1, 50}, {411, 6, 42}, {412, -4, 81}, {413, 1, 63},
    {414, -4, 70}, {415, 0, 67}, {416, 2, 57},
    {417, -2, 76}, {418, 11, 35}, {419, 4, 64}, {420, 1, 61},
    {421, 11, 35}, {422, 18, 25}, {423, 12, 24}, {424, 13, 29},
    {425, 13, 36},
    {426, -10, 93}, {427, -7, 73}, {428, -2, 73}, {429, 13, 46},
    {430, 9, 49}, {431, -7, 100}, {432, 9, 53}, {433, 2, 53},
    {434, 5, 53}, {435, -2, 61}
};

/*
 * This sparse bank covers the CABAC syntax helpers currently consumed by the
 * macroblock walker. Contexts outside this bank retain neutral probabilities
 * until their syntax paths are wired in with their table rows.
 */
static void cabac_apply_init_entries(avc_cabac_decoder_t *cabac,
                                     unsigned slice_qp_y,
                                     const cabac_ctx_init_entry_t *entries,
                                     unsigned entry_count)
{
    unsigned i;

    for (i = 0; i < entry_count; i++) {
        if (entries[i].ctx_idx < AVC_CABAC_CONTEXTS) {
            cabac_init_context_from_mn(&cabac->ctx[entries[i].ctx_idx],
                                       slice_qp_y, entries[i].m, entries[i].n);
        }
    }
}

static uint8_t cabac_read_bit(avc_cabac_decoder_t *cabac)
{
    size_t byte_pos = cabac->bit_pos >> 3;
    unsigned bit_in_byte = 7u - (unsigned)(cabac->bit_pos & 7u);

    if (byte_pos >= cabac->size) {
        cabac->error = 1;
        return 0;
    }
    cabac->bit_pos++;
    return (uint8_t)((cabac->data[byte_pos] >> bit_in_byte) & 1u);
}

void avc_cabac_init(avc_cabac_decoder_t *cabac, const uint8_t *data, size_t size)
{
    unsigned i;

    memset(cabac, 0, sizeof(*cabac));
    cabac->data = data;
    cabac->size = size;
    cabac->cod_i_range = 510;
    for (i = 0; i < 9; i++) {
        cabac->cod_i_offset = (cabac->cod_i_offset << 1) | cabac_read_bit(cabac);
    }
}

int avc_cabac_init_contexts(avc_cabac_decoder_t *cabac, unsigned slice_qp_y,
                            unsigned cabac_init_idc, unsigned slice_type)
{
    unsigned i;
    unsigned init_count = (unsigned)(sizeof(cabac_init_common) / sizeof(cabac_init_common[0]));

    if (slice_qp_y > 51 || cabac_init_idc > 2) {
        cabac->error = 1;
        return 0;
    }
    (void)slice_type;

    for (i = 0; i < AVC_CABAC_CONTEXTS; i++) {
        cabac->ctx[i].state = 10;
        cabac->ctx[i].mps = 0;
    }

    /*
     * TODO(ISO/IEC 14496-10 9.3.1.1): add the full P/B initialization
     * tables for cabac_init_idc 1 and 2. Until those staged tables land,
     * initialize the supported context subset from the idc-0/I rows rather
     * than leaving P/B slices at neutral probabilities.
     */
    cabac_apply_init_entries(cabac, slice_qp_y, cabac_init_common, init_count);
    return !cabac->error;
}

static void cabac_renorm(avc_cabac_decoder_t *cabac)
{
    while (cabac->cod_i_range < 256) {
        cabac->cod_i_range <<= 1;
        cabac->cod_i_offset <<= 1;
        cabac->cod_i_offset |= cabac_read_bit(cabac);
    }
}

int avc_cabac_decode_bypass(avc_cabac_decoder_t *cabac)
{
    cabac->cod_i_offset <<= 1;
    cabac->cod_i_offset |= cabac_read_bit(cabac);
    if (cabac->cod_i_offset >= cabac->cod_i_range) {
        cabac->cod_i_offset -= cabac->cod_i_range;
        return 1;
    }
    return 0;
}

int avc_cabac_decode_terminate(avc_cabac_decoder_t *cabac)
{
    cabac->cod_i_range -= 2;
    if (cabac->cod_i_offset >= cabac->cod_i_range) {
        return 1;
    }
    cabac_renorm(cabac);
    return 0;
}

int avc_cabac_decode_decision(avc_cabac_decoder_t *cabac, unsigned ctx_idx)
{
    avc_cabac_context_t *ctx;
    uint32_t cod_i_range_lps;
    unsigned q_cod_i_range_idx;
    int bin;

    if (ctx_idx >= AVC_CABAC_CONTEXTS) {
        cabac->error = 1;
        return 0;
    }
    ctx = &cabac->ctx[ctx_idx];

    q_cod_i_range_idx = (cabac->cod_i_range >> 6) & 3u;
    cod_i_range_lps = range_lps[ctx->state][q_cod_i_range_idx];
    cabac->cod_i_range -= cod_i_range_lps;
    if (cabac->cod_i_offset >= cabac->cod_i_range) {
        bin = 1 - ctx->mps;
        cabac->cod_i_offset -= cabac->cod_i_range;
        cabac->cod_i_range = cod_i_range_lps;
        if (ctx->state == 0) {
            ctx->mps = 1 - ctx->mps;
        }
        ctx->state = trans_idx_lps[ctx->state];
    } else {
        bin = ctx->mps;
        if (ctx->state < 62) {
            ctx->state++;
        }
    }
    cabac_renorm(cabac);
    return bin;
}

void avc_cabac_byte_align(avc_cabac_decoder_t *cabac)
{
    while ((cabac->bit_pos & 7u) != 0) {
        if (cabac_read_bit(cabac) != 0) {
            cabac->error = 1;
            return;
        }
    }
}

uint32_t avc_cabac_read_pcm_bits(avc_cabac_decoder_t *cabac, unsigned n)
{
    uint32_t value = 0;
    unsigned i;

    if (n > 32) {
        cabac->error = 1;
        return 0;
    }
    for (i = 0; i < n; i++) {
        value = (value << 1) | cabac_read_bit(cabac);
    }
    return value;
}

void avc_cabac_set_context(avc_cabac_decoder_t *cabac, unsigned ctx_idx,
                           uint8_t state, uint8_t mps)
{
    if (ctx_idx >= AVC_CABAC_CONTEXTS || state > 63 || mps > 1) {
        cabac->error = 1;
        return;
    }
    cabac->ctx[ctx_idx].state = state;
    cabac->ctx[ctx_idx].mps = mps;
}

static int decode_unary(avc_cabac_decoder_t *cabac, unsigned ctx_idx,
                        unsigned max_bins, unsigned *value)
{
    unsigned v = 0;

    while (v < max_bins) {
        int bin = avc_cabac_decode_decision(cabac, ctx_idx);
        if (cabac->error) {
            return 0;
        }
        if (!bin) {
            *value = v;
            return 1;
        }
        v++;
    }
    *value = v;
    return 1;
}

static int signed_from_cabac_unary(unsigned value)
{
    int signed_value = (int)((value + 1u) >> 1);
    return (value & 1u) ? signed_value : -signed_value;
}

int avc_cabac_decode_mb_skip_flag(avc_cabac_decoder_t *cabac, unsigned slice_type,
                                  int left_available, int left_skipped,
                                  int top_available, int top_skipped)
{
    unsigned ctx_idx = slice_type == 1 ? 24u : 11u;
    unsigned ctx_inc = 0;

    /*
     * ISO/IEC 14496-10 9.3.3.1.1.1: unavailable or skipped neighbors
     * contribute 0; available non-skipped neighbors contribute 1.
     */
    if (left_available && !left_skipped) {
        ctx_inc++;
    }
    if (top_available && !top_skipped) {
        ctx_inc++;
    }
    return avc_cabac_decode_decision(cabac, ctx_idx + ctx_inc);
}

int avc_cabac_decode_mb_type_i(avc_cabac_decoder_t *cabac, unsigned *mb_type)
{
    unsigned suffix;

    if (avc_cabac_decode_decision(cabac, 3) == 0) {
        *mb_type = 0;
        return !cabac->error;
    }
    if (!decode_unary(cabac, 4, 24, &suffix)) {
        return 0;
    }
    *mb_type = 1 + suffix;
    return !cabac->error;
}

int avc_cabac_decode_mb_type_p(avc_cabac_decoder_t *cabac, unsigned *mb_type)
{
    unsigned suffix;

    if (avc_cabac_decode_decision(cabac, 14) == 0) {
        *mb_type = 0;
        return !cabac->error;
    }
    if (!decode_unary(cabac, 15, 29, &suffix)) {
        return 0;
    }
    *mb_type = 1 + suffix;
    return !cabac->error;
}

int avc_cabac_decode_mb_type_b(avc_cabac_decoder_t *cabac, unsigned *mb_type)
{
    unsigned suffix;

    if (avc_cabac_decode_decision(cabac, 27) == 0) {
        *mb_type = 0;
        return !cabac->error;
    }
    if (!decode_unary(cabac, 28, 47, &suffix)) {
        return 0;
    }
    *mb_type = 1 + suffix;
    return !cabac->error;
}

int avc_cabac_decode_coded_block_pattern_luma(avc_cabac_decoder_t *cabac,
                                              int left_available, unsigned left_cbp_luma,
                                              int top_available, unsigned top_cbp_luma,
                                              unsigned *coded_block_pattern_luma)
{
    unsigned i;
    unsigned cbp = 0;

    for (i = 0; i < 4; i++) {
        unsigned ctx_inc = 0;
        int left_coded;
        int top_coded;

        if (i & 1u) {
            left_coded = ((cbp >> (i - 1u)) & 1u) != 0;
        } else {
            unsigned left_block = i + 1u;
            left_coded = left_available ? ((left_cbp_luma >> left_block) & 1u) != 0 : 0;
        }

        if (i >= 2u) {
            top_coded = ((cbp >> (i - 2u)) & 1u) != 0;
        } else {
            unsigned top_block = i + 2u;
            top_coded = top_available ? ((top_cbp_luma >> top_block) & 1u) != 0 : 0;
        }

        if (!left_coded) {
            ctx_inc++;
        }
        if (!top_coded) {
            ctx_inc += 2;
        }
        int bin = avc_cabac_decode_decision(cabac, 73 + ctx_inc);
        if (cabac->error) {
            return 0;
        }
        cbp |= (unsigned)bin << i;
    }
    *coded_block_pattern_luma = cbp;
    return 1;
}

int avc_cabac_decode_coded_block_pattern_chroma(avc_cabac_decoder_t *cabac,
                                                int left_available, unsigned left_cbp_chroma,
                                                int top_available, unsigned top_cbp_chroma,
                                                unsigned *coded_block_pattern_chroma)
{
    unsigned ctx_inc0 = 0;
    unsigned ctx_inc1 = 0;
    unsigned value = 0;
    int bin;

    if (!left_available || left_cbp_chroma == 0) {
        ctx_inc0++;
    }
    if (!top_available || top_cbp_chroma == 0) {
        ctx_inc0 += 2;
    }
    bin = avc_cabac_decode_decision(cabac, 77 + ctx_inc0);
    if (cabac->error) {
        return 0;
    }
    if (!bin) {
        *coded_block_pattern_chroma = 0;
        return 1;
    }

    value = 1;
    if (!left_available || left_cbp_chroma < 2) {
        ctx_inc1++;
    }
    if (!top_available || top_cbp_chroma < 2) {
        ctx_inc1 += 2;
    }
    bin = avc_cabac_decode_decision(cabac, 81 + ctx_inc1);
    if (cabac->error) {
        return 0;
    }
    if (bin) {
        value = 2;
    }
    *coded_block_pattern_chroma = value;
    return 1;
}

int avc_cabac_decode_transform_size_8x8_flag(avc_cabac_decoder_t *cabac,
                                             int left_transform_8x8,
                                             int top_transform_8x8)
{
    unsigned ctx_inc = 0;

    if (left_transform_8x8) {
        ctx_inc++;
    }
    if (top_transform_8x8) {
        ctx_inc++;
    }
    return avc_cabac_decode_decision(cabac, 399 + ctx_inc);
}

int avc_cabac_decode_mb_qp_delta(avc_cabac_decoder_t *cabac,
                                 int previous_mb_qp_delta_nonzero,
                                 int *mb_qp_delta)
{
    unsigned prefix = 0;
    unsigned ctx_idx = previous_mb_qp_delta_nonzero ? 61u : 60u;

    if (avc_cabac_decode_decision(cabac, ctx_idx) == 0) {
        *mb_qp_delta = 0;
        return !cabac->error;
    }
    prefix = 1;
    while (prefix < 128) {
        int bin = avc_cabac_decode_decision(cabac, 62);
        if (cabac->error) {
            return 0;
        }
        if (!bin) {
            *mb_qp_delta = signed_from_cabac_unary(prefix);
            return 1;
        }
        prefix++;
    }
    cabac->error = 1;
    return 0;
}

int avc_cabac_decode_prev_intra_pred_mode_flag(avc_cabac_decoder_t *cabac)
{
    return avc_cabac_decode_decision(cabac, 68);
}

int avc_cabac_decode_rem_intra_pred_mode(avc_cabac_decoder_t *cabac,
                                         unsigned *mode)
{
    unsigned i;
    unsigned value = 0;

    /*
     * ISO/IEC 14496-10 7.3.5.1: rem_intra4x4_pred_mode and
     * rem_intra8x8_pred_mode are 3-bin syntax elements when the previous
     * prediction mode flag is zero. CABAC uses the intra prediction mode
     * context around ctxIdx 69 for these bins.
     */
    for (i = 0; i < 3; i++) {
        value = (value << 1) | (unsigned)avc_cabac_decode_decision(cabac, 69);
        if (cabac->error) {
            return 0;
        }
    }
    *mode = value;
    return 1;
}

int avc_cabac_decode_intra_chroma_pred_mode(avc_cabac_decoder_t *cabac,
                                            unsigned *mode)
{
    unsigned value;

    if (!decode_unary(cabac, 64, 3, &value)) {
        return 0;
    }
    *mode = value;
    return 1;
}

int avc_cabac_decode_ref_idx_l0(avc_cabac_decoder_t *cabac,
                                int left_nonzero, int top_nonzero,
                                unsigned *ref_idx)
{
    unsigned value;
    unsigned ctx_inc = 0;

    if (left_nonzero) {
        ctx_inc++;
    }
    if (top_nonzero) {
        ctx_inc++;
    }
    if (avc_cabac_decode_decision(cabac, 54 + ctx_inc) == 0) {
        *ref_idx = 0;
        return !cabac->error;
    }

    value = 1;
    while (value < 32) {
        int bin = avc_cabac_decode_decision(cabac, 58);
        if (cabac->error) {
            return 0;
        }
        if (!bin) {
            *ref_idx = value;
            return 1;
        }
        value++;
    }
    cabac->error = 1;
    return 0;
}

int avc_cabac_decode_ref_idx_l1(avc_cabac_decoder_t *cabac,
                                int left_nonzero, int top_nonzero,
                                unsigned *ref_idx)
{
    return avc_cabac_decode_ref_idx_l0(cabac, left_nonzero, top_nonzero, ref_idx);
}

int avc_cabac_decode_mvd_component(avc_cabac_decoder_t *cabac,
                                   unsigned ctx_base,
                                   unsigned abs_mvd_left,
                                   unsigned abs_mvd_top,
                                   int16_t *mvd)
{
    unsigned e = abs_mvd_left + abs_mvd_top;
    unsigned ctx_inc = e < 3 ? 0u : (e > 32 ? 2u : 1u);
    unsigned prefix = 0;
    unsigned suffix = 0;
    int sign;

    if (avc_cabac_decode_decision(cabac, ctx_base + ctx_inc) == 0) {
        *mvd = 0;
        return !cabac->error;
    }

    prefix = 1;
    while (prefix < 9) {
        int bin = avc_cabac_decode_decision(cabac, ctx_base + 3 + (prefix > 1));
        if (cabac->error) {
            return 0;
        }
        if (!bin) {
            break;
        }
        prefix++;
    }
    if (prefix >= 9) {
        unsigned k = 0;
        while (avc_cabac_decode_bypass(cabac)) {
            k++;
            if (k > 20) {
                cabac->error = 1;
                return 0;
            }
        }
        while (k > 0) {
            suffix = (suffix << 1) | (unsigned)avc_cabac_decode_bypass(cabac);
            k--;
        }
    }
    sign = avc_cabac_decode_bypass(cabac) ? -1 : 1;
    if (prefix + suffix > 32767u) {
        cabac->error = 1;
        return 0;
    }
    *mvd = (int16_t)(sign * (int)(prefix + suffix));
    return !cabac->error;
}

int avc_cabac_decode_sub_mb_type_p(avc_cabac_decoder_t *cabac,
                                   unsigned *sub_mb_type)
{
    unsigned suffix;

    if (avc_cabac_decode_decision(cabac, 21) == 0) {
        *sub_mb_type = 0;
        return !cabac->error;
    }
    if (!decode_unary(cabac, 22, 3, &suffix)) {
        return 0;
    }
    *sub_mb_type = 1 + suffix;
    return !cabac->error;
}

int avc_cabac_decode_sub_mb_type_b(avc_cabac_decoder_t *cabac,
                                   unsigned *sub_mb_type)
{
    unsigned suffix;

    if (avc_cabac_decode_decision(cabac, 36) == 0) {
        *sub_mb_type = 0;
        return !cabac->error;
    }
    if (!decode_unary(cabac, 37, 12, &suffix)) {
        return 0;
    }
    *sub_mb_type = 1 + suffix;
    return !cabac->error;
}

int avc_cabac_decode_coded_block_flag(avc_cabac_decoder_t *cabac,
                                      unsigned ctx_base,
                                      int left_coded,
                                      int top_coded)
{
    unsigned ctx_inc = 0;

    /*
     * ISO/IEC 14496-10 9.3.3.1.1.9: coded_block_flag is present before
     * coefficient flags. Neighbor availability/category derivation happens in
     * the macroblock layer; this helper keeps the bin decode table-local.
     */
    if (left_coded) {
        ctx_inc++;
    }
    if (top_coded) {
        ctx_inc += 2;
    }
    if (ctx_base + ctx_inc >= AVC_CABAC_CONTEXTS) {
        cabac->error = 1;
        return 0;
    }
    return avc_cabac_decode_decision(cabac, ctx_base + ctx_inc);
}

int avc_cabac_decode_significant_coeff_flag(avc_cabac_decoder_t *cabac,
                                            unsigned ctx_base, unsigned scan_index)
{
    if (scan_index > 63 || ctx_base + scan_index >= AVC_CABAC_CONTEXTS) {
        cabac->error = 1;
        return 0;
    }
    return avc_cabac_decode_decision(cabac, ctx_base + scan_index);
}

int avc_cabac_decode_last_significant_coeff_flag(avc_cabac_decoder_t *cabac,
                                                 unsigned ctx_base, unsigned scan_index)
{
    if (scan_index > 63 || ctx_base + scan_index >= AVC_CABAC_CONTEXTS) {
        cabac->error = 1;
        return 0;
    }
    return avc_cabac_decode_decision(cabac, ctx_base + scan_index);
}

int avc_cabac_decode_coeff_abs_level_minus1_stateful(avc_cabac_decoder_t *cabac,
                                                     unsigned ctx_base,
                                                     unsigned num_abs_level_eq1,
                                                     unsigned num_abs_level_gt1,
                                                     unsigned *value)
{
    unsigned prefix;
    unsigned suffix = 0;
    unsigned suffix_bits = 0;
    unsigned first_ctx_inc;
    unsigned rest_ctx_inc;

    first_ctx_inc = num_abs_level_gt1 ? 0u : 1u + (num_abs_level_eq1 > 3u ? 3u : num_abs_level_eq1);
    rest_ctx_inc = 5u + (num_abs_level_gt1 > 4u ? 4u : num_abs_level_gt1);

    if (ctx_base + rest_ctx_inc >= AVC_CABAC_CONTEXTS) {
        cabac->error = 1;
        return 0;
    }

    if (avc_cabac_decode_decision(cabac, ctx_base + first_ctx_inc) == 0) {
        *value = 0;
        return !cabac->error;
    }

    prefix = 1;
    while (prefix < 14) {
        int bin = avc_cabac_decode_decision(cabac, ctx_base + rest_ctx_inc);
        if (cabac->error) {
            return 0;
        }
        if (!bin) {
            *value = prefix;
            return 1;
        }
        prefix++;
    }

    if (prefix >= 14) {
        while (avc_cabac_decode_bypass(cabac)) {
            suffix_bits++;
            if (suffix_bits > 24) {
                cabac->error = 1;
                return 0;
            }
        }
        while (suffix_bits > 0) {
            suffix = (suffix << 1) | (unsigned)avc_cabac_decode_bypass(cabac);
            suffix_bits--;
        }
    }
    *value = prefix + suffix;
    return !cabac->error;
}

int avc_cabac_decode_coeff_abs_level_minus1(avc_cabac_decoder_t *cabac,
                                            unsigned ctx_base, unsigned *value)
{
    return avc_cabac_decode_coeff_abs_level_minus1_stateful(cabac, ctx_base, 0, 0, value);
}

int avc_cabac_decode_residual_block(avc_cabac_decoder_t *cabac,
                                    unsigned max_coeff,
                                    unsigned coded_ctx_base,
                                    unsigned sig_ctx_base,
                                    unsigned last_ctx_base,
                                    unsigned level_ctx_base,
                                    int left_coded,
                                    int top_coded,
                                    avc_cabac_residual_block_t *block)
{
    unsigned i;
    unsigned coeff_count = 0;
    unsigned num_abs_level_eq1 = 0;
    unsigned num_abs_level_gt1 = 0;

    if (max_coeff == 0 || max_coeff > 64) {
        cabac->error = 1;
        return 0;
    }
    memset(block, 0, sizeof(*block));
    block->max_coeff = max_coeff;

    if (!avc_cabac_decode_coded_block_flag(cabac, coded_ctx_base, left_coded, top_coded)) {
        if (cabac->error) {
            return 0;
        }
        return 1;
    }

    for (i = 0; i < max_coeff; i++) {
        int significant = avc_cabac_decode_significant_coeff_flag(cabac, sig_ctx_base, i);
        if (cabac->error) {
            return 0;
        }
        block->significant[i] = (uint8_t)significant;
        if (significant) {
            int last = avc_cabac_decode_last_significant_coeff_flag(cabac, last_ctx_base, i);
            if (cabac->error) {
                return 0;
            }
            block->last_significant[i] = (uint8_t)last;
            coeff_count++;
            if (last) {
                break;
            }
        }
    }

    while (coeff_count > 0) {
        unsigned scan = max_coeff;
        unsigned abs_minus1 = 0;
        int sign;

        for (i = max_coeff; i > 0; i--) {
            if (block->significant[i - 1] && block->coeff_level[i - 1] == 0) {
                scan = i - 1;
                break;
            }
        }
        if (scan == max_coeff) {
            cabac->error = 1;
            return 0;
        }
        if (!avc_cabac_decode_coeff_abs_level_minus1_stateful(cabac, level_ctx_base,
                                                              num_abs_level_eq1,
                                                              num_abs_level_gt1,
                                                              &abs_minus1)) {
            return 0;
        }
        sign = avc_cabac_decode_bypass(cabac) ? -1 : 1;
        block->coeff_level[scan] = sign * (int)(abs_minus1 + 1u);
        if (abs_minus1 == 0) {
            num_abs_level_eq1++;
        } else {
            num_abs_level_gt1++;
        }
        coeff_count--;
    }

    for (i = 0; i < max_coeff; i++) {
        if (block->coeff_level[i] != 0) {
            block->total_coeff++;
        }
    }
    return !cabac->error;
}
