#include "avc/avc_macroblock.h"
#include "avc/avc_bitreader.h"
#include "avc/avc_cabac.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int available;
    int skipped;
    int transform_size_8x8_flag;
    int has_pred;
    uint32_t mb_type;
    uint32_t coded_block_pattern_luma;
    uint32_t coded_block_pattern_chroma;
    int32_t mb_qp_delta;
    uint8_t luma_nonzero[16];
    uint8_t luma_8x8_nonzero[4];
    uint8_t luma_16x16_dc_nonzero;
    uint8_t chroma_dc_nonzero[2];
    uint8_t chroma_ac_nonzero[2][16];
    avc_mb_pred_event_t pred;
} avc_mb_state_t;

typedef struct {
    avc_mb_pred_kind_t pred_kind;
    uint32_t cbp_luma;
    uint32_t cbp_chroma;
    int is_pcm;
} avc_i_mb_type_info_t;

typedef enum {
    AVC_P_MB_UNKNOWN = 0,
    AVC_P_MB_L0_16X16,
    AVC_P_MB_L0_L0_16X8,
    AVC_P_MB_L0_L0_8X16,
    AVC_P_MB_8X8,
    AVC_P_MB_8X8REF0,
    AVC_P_MB_INTRA
} avc_p_mb_shape_t;

typedef struct {
    avc_p_mb_shape_t shape;
    avc_i_mb_type_info_t intra;
} avc_p_mb_type_info_t;

typedef enum {
    AVC_B_MB_UNKNOWN = 0,
    AVC_B_MB_DIRECT,
    AVC_B_MB_INTER,
    AVC_B_MB_8X8,
    AVC_B_MB_INTRA
} avc_b_mb_shape_t;

typedef struct {
    avc_b_mb_shape_t shape;
    unsigned partition_count;
    uint8_t list_mask[4];
    avc_i_mb_type_info_t intra;
} avc_b_mb_type_info_t;

typedef struct {
    avc_residual_kind_t kind;
    unsigned max_coeff;
    unsigned coded_ctx_base;
    unsigned sig_ctx_base;
    unsigned last_ctx_base;
    unsigned level_ctx_base;
} avc_cabac_residual_plan_t;

static const avc_cabac_residual_plan_t residual_plans[] = {
    {AVC_RESIDUAL_LUMA_4X4, 16, 93, 134, 195, 247},
    {AVC_RESIDUAL_LUMA_8X8, 64, 1012, 402, 417, 426},
    {AVC_RESIDUAL_LUMA_16X16_DC, 16, 85, 105, 166, 227},
    {AVC_RESIDUAL_LUMA_16X16_AC, 15, 89, 120, 181, 237},
    {AVC_RESIDUAL_CHROMA_DC, 4, 97, 149, 210, 257},
    {AVC_RESIDUAL_CHROMA_AC, 15, 101, 152, 213, 266}
};

static unsigned chroma_array_type(const avc_sps_t *sps);
static unsigned chroma_ac_blocks_per_component(unsigned chroma_format_idc);

static void note(avc_macroblock_callbacks_t callbacks, void *opaque, const char *message)
{
    if (callbacks.on_note) {
        callbacks.on_note(opaque, message);
    }
}

static void notef(avc_macroblock_callbacks_t callbacks, void *opaque,
                  const char *fmt, ...)
{
    char message[192];
    va_list ap;

    if (!callbacks.on_note) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    callbacks.on_note(opaque, message);
}

static uint32_t pic_size_in_mbs(const avc_sps_t *sps)
{
    uint32_t width = sps->pic_width_in_mbs_minus1 + 1u;
    uint32_t height = (sps->pic_height_in_map_units_minus1 + 1u) * (2u - sps->frame_mbs_only_flag);
    return width * height;
}

static uint32_t pic_width_in_mbs(const avc_sps_t *sps)
{
    return sps->pic_width_in_mbs_minus1 + 1u;
}

static avc_mb_state_t *state_for(avc_mb_state_t *states, uint32_t count, uint32_t address)
{
    if (address >= count) {
        return NULL;
    }
    return &states[address];
}

static void neighbor_states(avc_mb_state_t *states, uint32_t count, uint32_t width,
                            uint32_t address,
                            const avc_mb_state_t **left,
                            const avc_mb_state_t **top)
{
    *left = NULL;
    *top = NULL;
    if (address > 0 && (address % width) != 0) {
        *left = state_for(states, count, address - 1);
    }
    if (address >= width) {
        *top = state_for(states, count, address - width);
    }
}

static void save_mb_state(avc_mb_state_t *states, uint32_t count,
                          const avc_macroblock_event_t *event,
                          const avc_mb_pred_event_t *pred)
{
    avc_mb_state_t *state = state_for(states, count, event->address);
    if (!state) {
        return;
    }
    state->available = 1;
    state->skipped = event->skipped;
    state->transform_size_8x8_flag = event->transform_size_8x8_flag;
    state->mb_type = event->mb_type;
    state->coded_block_pattern_luma = event->coded_block_pattern_luma;
    state->coded_block_pattern_chroma = event->coded_block_pattern_chroma;
    state->mb_qp_delta = event->mb_qp_delta;
    if (pred) {
        state->has_pred = 1;
        state->pred = *pred;
    }
}

static unsigned luma4x4_index_from_xy(unsigned x, unsigned y)
{
    return ((y >> 1) * 2u + (x >> 1)) * 4u + ((y & 1u) << 1) + (x & 1u);
}

static void luma4x4_xy_from_index(unsigned index, unsigned *x, unsigned *y)
{
    unsigned group = index >> 2;
    unsigned sub = index & 3u;

    *x = ((group & 1u) << 1) + (sub & 1u);
    *y = ((group >> 1) << 1) + (sub >> 1);
}

static int derive_nc_from_neighbors(int left_available, unsigned left_count,
                                    int top_available, unsigned top_count)
{
    if (left_available && top_available) {
        return (int)((left_count + top_count + 1u) >> 1);
    }
    if (left_available) {
        return (int)left_count;
    }
    if (top_available) {
        return (int)top_count;
    }
    return 0;
}

static int cavlc_luma_nC(const avc_mb_state_t *curr,
                         const avc_mb_state_t *left,
                         const avc_mb_state_t *top,
                         unsigned block_index)
{
    unsigned x;
    unsigned y;
    int left_available;
    int top_available;
    unsigned left_count = 0;
    unsigned top_count = 0;

    if (block_index >= 16) {
        return 0;
    }
    luma4x4_xy_from_index(block_index, &x, &y);

    if (x > 0) {
        left_available = 1;
        left_count = curr->luma_nonzero[luma4x4_index_from_xy(x - 1u, y)];
    } else {
        left_available = left && left->available;
        if (left_available) {
            left_count = left->luma_nonzero[luma4x4_index_from_xy(3, y)];
        }
    }

    if (y > 0) {
        top_available = 1;
        top_count = curr->luma_nonzero[luma4x4_index_from_xy(x, y - 1u)];
    } else {
        top_available = top && top->available;
        if (top_available) {
            top_count = top->luma_nonzero[luma4x4_index_from_xy(x, 3)];
        }
    }

    return derive_nc_from_neighbors(left_available, left_count,
                                    top_available, top_count);
}

static unsigned chroma_ac_width(unsigned chroma_format)
{
    return chroma_format == 3 ? 4u : 2u;
}

static int cavlc_chroma_ac_nC(const avc_mb_state_t *curr,
                              const avc_mb_state_t *left,
                              const avc_mb_state_t *top,
                              unsigned chroma_format,
                              unsigned component,
                              unsigned block)
{
    unsigned width = chroma_ac_width(chroma_format);
    unsigned height = chroma_ac_blocks_per_component(chroma_format) / width;
    unsigned x;
    unsigned y;
    int left_available;
    int top_available;
    unsigned left_count = 0;
    unsigned top_count = 0;

    if (component >= 2 || block >= 16 || width == 0 || height == 0) {
        return 0;
    }
    x = block % width;
    y = block / width;

    if (x > 0) {
        left_available = 1;
        left_count = curr->chroma_ac_nonzero[component][block - 1u];
    } else {
        left_available = left && left->available;
        if (left_available) {
            left_count = left->chroma_ac_nonzero[component][y * width + (width - 1u)];
        }
    }

    if (y > 0) {
        top_available = 1;
        top_count = curr->chroma_ac_nonzero[component][block - width];
    } else {
        top_available = top && top->available;
        if (top_available) {
            top_count = top->chroma_ac_nonzero[component][(height - 1u) * width + x];
        }
    }

    return derive_nc_from_neighbors(left_available, left_count,
                                    top_available, top_count);
}

static uint8_t clipped_nonzero_count(unsigned total_coeff)
{
    return (uint8_t)(total_coeff > 255u ? 255u : total_coeff);
}

static void store_residual_nonzero(avc_mb_state_t *curr,
                                   avc_residual_kind_t kind,
                                   unsigned block_index,
                                   unsigned total_coeff,
                                   unsigned chroma_format)
{
    unsigned ac_blocks;

    if (!curr) {
        return;
    }

    switch (kind) {
    case AVC_RESIDUAL_LUMA_4X4:
    case AVC_RESIDUAL_LUMA_16X16_AC:
        if (block_index < 16) {
            curr->luma_nonzero[block_index] = clipped_nonzero_count(total_coeff);
        }
        break;
    case AVC_RESIDUAL_LUMA_8X8:
        if (block_index < 4) {
            unsigned base = block_index * 4u;
            uint8_t count = clipped_nonzero_count(total_coeff);
            curr->luma_8x8_nonzero[block_index] = count;
            curr->luma_nonzero[base + 0u] = count;
            curr->luma_nonzero[base + 1u] = count;
            curr->luma_nonzero[base + 2u] = count;
            curr->luma_nonzero[base + 3u] = count;
        }
        break;
    case AVC_RESIDUAL_LUMA_16X16_DC:
        curr->luma_16x16_dc_nonzero = clipped_nonzero_count(total_coeff);
        break;
    case AVC_RESIDUAL_CHROMA_DC:
        if (block_index < 2) {
            curr->chroma_dc_nonzero[block_index] = clipped_nonzero_count(total_coeff);
        }
        break;
    case AVC_RESIDUAL_CHROMA_AC:
        ac_blocks = chroma_ac_blocks_per_component(chroma_format);
        if (ac_blocks > 0) {
            unsigned component = block_index / ac_blocks;
            unsigned block = block_index % ac_blocks;
            if (component < 2 && block < 16) {
                curr->chroma_ac_nonzero[component][block] = clipped_nonzero_count(total_coeff);
            }
        }
        break;
    }
}

static int cabac_luma_cbf_neighbors(const avc_mb_state_t *curr,
                                    const avc_mb_state_t *left,
                                    const avc_mb_state_t *top,
                                    unsigned block_index,
                                    int transform_size_8x8_flag,
                                    int *left_coded,
                                    int *top_coded)
{
    unsigned x;
    unsigned y;

    *left_coded = 1;
    *top_coded = 1;

    if (!curr || block_index >= 16) {
        return 0;
    }

    if (transform_size_8x8_flag) {
        unsigned group = block_index & 3u;
        x = group & 1u;
        y = group >> 1;
        if (x > 0) {
            *left_coded = curr->luma_8x8_nonzero[group - 1u] != 0;
        } else if (left && left->available) {
            *left_coded = left->luma_8x8_nonzero[group + 1u] != 0;
        }
        if (y > 0) {
            *top_coded = curr->luma_8x8_nonzero[group - 2u] != 0;
        } else if (top && top->available) {
            *top_coded = top->luma_8x8_nonzero[group + 2u] != 0;
        }
        return 1;
    }

    luma4x4_xy_from_index(block_index, &x, &y);
    if (x > 0) {
        *left_coded = curr->luma_nonzero[luma4x4_index_from_xy(x - 1u, y)] != 0;
    } else if (left && left->available) {
        *left_coded = left->luma_nonzero[luma4x4_index_from_xy(3, y)] != 0;
    }
    if (y > 0) {
        *top_coded = curr->luma_nonzero[luma4x4_index_from_xy(x, y - 1u)] != 0;
    } else if (top && top->available) {
        *top_coded = top->luma_nonzero[luma4x4_index_from_xy(x, 3)] != 0;
    }
    return 1;
}

static int cabac_luma16_dc_cbf_neighbors(const avc_mb_state_t *left,
                                         const avc_mb_state_t *top,
                                         int *left_coded,
                                         int *top_coded)
{
    *left_coded = left && left->available ? left->luma_16x16_dc_nonzero != 0 : 1;
    *top_coded = top && top->available ? top->luma_16x16_dc_nonzero != 0 : 1;
    return 1;
}

static int cabac_chroma_dc_cbf_neighbors(const avc_mb_state_t *left,
                                         const avc_mb_state_t *top,
                                         unsigned component,
                                         int *left_coded,
                                         int *top_coded)
{
    if (component >= 2) {
        return 0;
    }
    *left_coded = left && left->available ? left->chroma_dc_nonzero[component] != 0 : 1;
    *top_coded = top && top->available ? top->chroma_dc_nonzero[component] != 0 : 1;
    return 1;
}

static int cabac_chroma_ac_cbf_neighbors(const avc_mb_state_t *curr,
                                         const avc_mb_state_t *left,
                                         const avc_mb_state_t *top,
                                         unsigned chroma_format,
                                         unsigned component,
                                         unsigned block,
                                         int *left_coded,
                                         int *top_coded)
{
    unsigned width = chroma_ac_width(chroma_format);
    unsigned height = width ? chroma_ac_blocks_per_component(chroma_format) / width : 0;
    unsigned x;
    unsigned y;

    *left_coded = 1;
    *top_coded = 1;

    if (!curr || component >= 2 || block >= 16 || width == 0 || height == 0) {
        return 0;
    }
    x = block % width;
    y = block / width;

    if (x > 0) {
        *left_coded = curr->chroma_ac_nonzero[component][block - 1u] != 0;
    } else if (left && left->available) {
        *left_coded = left->chroma_ac_nonzero[component][y * width + (width - 1u)] != 0;
    }
    if (y > 0) {
        *top_coded = curr->chroma_ac_nonzero[component][block - width] != 0;
    } else if (top && top->available) {
        *top_coded = top->chroma_ac_nonzero[component][(height - 1u) * width + x] != 0;
    }
    return 1;
}

static void cavlc_mark_ipcm_nonzero(avc_mb_state_t *state, const avc_sps_t *sps)
{
    unsigned i;
    unsigned chroma_format = chroma_array_type(sps);
    unsigned ac_blocks = chroma_ac_blocks_per_component(chroma_format);

    if (!state) {
        return;
    }
    for (i = 0; i < 16; i++) {
        state->luma_nonzero[i] = 16;
    }
    for (i = 0; i < 4; i++) {
        state->luma_8x8_nonzero[i] = 16;
    }
    state->luma_16x16_dc_nonzero = 16;
    for (i = 0; i < 2; i++) {
        unsigned block;
        state->chroma_dc_nonzero[i] = chroma_format == 0 ? 0 : 16;
        for (block = 0; block < ac_blocks && block < 16; block++) {
            state->chroma_ac_nonzero[i][block] = 15;
        }
    }
}

static avc_i_mb_type_info_t classify_i_mb_type(uint32_t mb_type)
{
    avc_i_mb_type_info_t info;
    uint32_t coded_block_pattern;

    info = (avc_i_mb_type_info_t){0};
    if (mb_type == 0) {
        info.pred_kind = AVC_MB_PRED_INTRA_4X4;
        return info;
    }
    if (mb_type == 25) {
        info.pred_kind = AVC_MB_PRED_PCM;
        info.is_pcm = 1;
        return info;
    }
    if (mb_type > 25) {
        info.pred_kind = AVC_MB_PRED_UNKNOWN;
        return info;
    }

    info.pred_kind = AVC_MB_PRED_INTRA_16X16;
    coded_block_pattern = mb_type - 1u;
    info.cbp_chroma = (coded_block_pattern / 4u) % 3u;
    info.cbp_luma = coded_block_pattern >= 12u ? 15u : 0u;
    return info;
}

static avc_p_mb_type_info_t classify_p_mb_type(uint32_t mb_type)
{
    avc_p_mb_type_info_t info;

    info = (avc_p_mb_type_info_t){0};
    if (mb_type == 0) {
        info.shape = AVC_P_MB_L0_16X16;
    } else if (mb_type == 1) {
        info.shape = AVC_P_MB_L0_L0_16X8;
    } else if (mb_type == 2) {
        info.shape = AVC_P_MB_L0_L0_8X16;
    } else if (mb_type == 3) {
        info.shape = AVC_P_MB_8X8;
    } else if (mb_type == 4) {
        info.shape = AVC_P_MB_8X8REF0;
    } else if (mb_type >= 5 && mb_type <= 30) {
        info.shape = AVC_P_MB_INTRA;
        info.intra = classify_i_mb_type(mb_type - 5u);
    }
    return info;
}

static avc_b_mb_type_info_t classify_b_mb_type(uint32_t mb_type)
{
    static const uint8_t masks[22][2] = {
        {0, 0}, {1, 0}, {2, 0}, {3, 0},
        {1, 1}, {1, 1}, {2, 2}, {2, 2},
        {1, 2}, {1, 2}, {2, 1}, {2, 1},
        {1, 3}, {1, 3}, {2, 3}, {2, 3},
        {3, 1}, {3, 1}, {3, 2}, {3, 2},
        {3, 3}, {3, 3}
    };
    avc_b_mb_type_info_t info;

    info = (avc_b_mb_type_info_t){0};
    if (mb_type == 0) {
        info.shape = AVC_B_MB_DIRECT;
        info.partition_count = 1;
    } else if (mb_type >= 1 && mb_type <= 21) {
        info.shape = AVC_B_MB_INTER;
        info.partition_count = 2;
        info.list_mask[0] = masks[mb_type][0];
        info.list_mask[1] = masks[mb_type][1];
    } else if (mb_type == 22) {
        info.shape = AVC_B_MB_8X8;
        info.partition_count = 4;
    } else if (mb_type >= 23 && mb_type <= 48) {
        info.shape = AVC_B_MB_INTRA;
        info.intra = classify_i_mb_type(mb_type - 23u);
    }
    return info;
}

static unsigned p_inter_partition_count(avc_p_mb_shape_t shape)
{
    switch (shape) {
    case AVC_P_MB_L0_16X16: return 1;
    case AVC_P_MB_L0_L0_16X8: return 2;
    case AVC_P_MB_L0_L0_8X16: return 2;
    case AVC_P_MB_8X8: return 4;
    case AVC_P_MB_8X8REF0: return 4;
    default: return 0;
    }
}

static uint8_t b_sub_mb_list_mask(unsigned sub_mb_type)
{
    switch (sub_mb_type) {
    case 0: return 0;
    case 1: case 4: case 5: case 10: return 1;
    case 2: case 6: case 7: case 11: return 2;
    case 3: case 8: case 9: case 12: return 3;
    default: return 0;
    }
}

static int b_sub_mb_is_direct(unsigned sub_mb_type)
{
    return sub_mb_type == 0;
}

static unsigned abs_i16(int16_t value)
{
    return value < 0 ? (unsigned)(-value) : (unsigned)value;
}

static unsigned neighbor_ref_idx_l0(const avc_mb_state_t *neighbor, unsigned partition)
{
    if (!neighbor || !neighbor->available || !neighbor->has_pred ||
        neighbor->pred.kind != AVC_MB_PRED_INTER || neighbor->pred.partition_count == 0) {
        return 0;
    }
    if (partition >= neighbor->pred.partition_count) {
        partition = neighbor->pred.partition_count - 1u;
    }
    return neighbor->pred.ref_idx_l0[partition];
}

static unsigned neighbor_ref_idx_l1(const avc_mb_state_t *neighbor, unsigned partition)
{
    if (!neighbor || !neighbor->available || !neighbor->has_pred ||
        neighbor->pred.kind != AVC_MB_PRED_INTER || neighbor->pred.partition_count == 0) {
        return 0;
    }
    if (partition >= neighbor->pred.partition_count) {
        partition = neighbor->pred.partition_count - 1u;
    }
    return neighbor->pred.ref_idx_l1[partition];
}

static unsigned pred_ref_idx_l0(const avc_mb_pred_event_t *pred, unsigned partition)
{
    if (!pred || pred->partition_count == 0 || partition >= pred->partition_count) {
        return 0;
    }
    return pred->ref_idx_l0[partition];
}

static unsigned pred_ref_idx_l1(const avc_mb_pred_event_t *pred, unsigned partition)
{
    if (!pred || pred->partition_count == 0 || partition >= pred->partition_count) {
        return 0;
    }
    return pred->ref_idx_l1[partition];
}

static unsigned neighbor_abs_mvd_l0(const avc_mb_state_t *neighbor,
                                    unsigned partition,
                                    unsigned component)
{
    if (!neighbor || !neighbor->available || !neighbor->has_pred ||
        neighbor->pred.kind != AVC_MB_PRED_INTER || neighbor->pred.partition_count == 0 ||
        component > 1) {
        return 0;
    }
    if (partition >= neighbor->pred.partition_count) {
        partition = neighbor->pred.partition_count - 1u;
    }
    return abs_i16(neighbor->pred.mvd_l0[partition][component]);
}

static unsigned neighbor_abs_mvd_l1(const avc_mb_state_t *neighbor,
                                    unsigned partition,
                                    unsigned component)
{
    if (!neighbor || !neighbor->available || !neighbor->has_pred ||
        neighbor->pred.kind != AVC_MB_PRED_INTER || neighbor->pred.partition_count == 0 ||
        component > 1) {
        return 0;
    }
    if (partition >= neighbor->pred.partition_count) {
        partition = neighbor->pred.partition_count - 1u;
    }
    return abs_i16(neighbor->pred.mvd_l1[partition][component]);
}

static unsigned pred_abs_mvd_l0(const avc_mb_pred_event_t *pred,
                                unsigned partition,
                                unsigned component)
{
    if (!pred || pred->partition_count == 0 || partition >= pred->partition_count ||
        component > 1) {
        return 0;
    }
    return abs_i16(pred->mvd_l0[partition][component]);
}

static unsigned pred_abs_mvd_l1(const avc_mb_pred_event_t *pred,
                                unsigned partition,
                                unsigned component)
{
    if (!pred || pred->partition_count == 0 || partition >= pred->partition_count ||
        component > 1) {
        return 0;
    }
    return abs_i16(pred->mvd_l1[partition][component]);
}

static int16_t median_i16(int16_t a, int16_t b, int16_t c)
{
    if ((a <= b && b <= c) || (c <= b && b <= a)) {
        return b;
    }
    if ((b <= a && a <= c) || (c <= a && a <= b)) {
        return a;
    }
    return c;
}

static int neighbor_mv_l0(const avc_mb_state_t *neighbor,
                          unsigned partition,
                          unsigned ref_idx,
                          int16_t mv[2])
{
    if (!neighbor || !neighbor->available || !neighbor->has_pred ||
        neighbor->pred.kind != AVC_MB_PRED_INTER || neighbor->pred.partition_count == 0) {
        mv[0] = 0;
        mv[1] = 0;
        return 0;
    }
    if (partition >= neighbor->pred.partition_count) {
        partition = neighbor->pred.partition_count - 1u;
    }
    if ((neighbor->pred.list_mask[partition] & 1u) == 0 ||
        neighbor->pred.ref_idx_l0[partition] != ref_idx) {
        mv[0] = 0;
        mv[1] = 0;
        return 0;
    }
    mv[0] = neighbor->pred.mv_l0[partition][0];
    mv[1] = neighbor->pred.mv_l0[partition][1];
    return 1;
}

static int neighbor_mv_l1(const avc_mb_state_t *neighbor,
                          unsigned partition,
                          unsigned ref_idx,
                          int16_t mv[2])
{
    if (!neighbor || !neighbor->available || !neighbor->has_pred ||
        neighbor->pred.kind != AVC_MB_PRED_INTER || neighbor->pred.partition_count == 0) {
        mv[0] = 0;
        mv[1] = 0;
        return 0;
    }
    if (partition >= neighbor->pred.partition_count) {
        partition = neighbor->pred.partition_count - 1u;
    }
    if ((neighbor->pred.list_mask[partition] & 2u) == 0 ||
        neighbor->pred.ref_idx_l1[partition] != ref_idx) {
        mv[0] = 0;
        mv[1] = 0;
        return 0;
    }
    mv[0] = neighbor->pred.mv_l1[partition][0];
    mv[1] = neighbor->pred.mv_l1[partition][1];
    return 1;
}

static int pred_mv_l0(const avc_mb_pred_event_t *pred,
                      unsigned partition,
                      unsigned ref_idx,
                      int16_t mv[2])
{
    if (!pred || pred->partition_count == 0 || partition >= pred->partition_count ||
        (pred->list_mask[partition] & 1u) == 0 || pred->ref_idx_l0[partition] != ref_idx) {
        mv[0] = 0;
        mv[1] = 0;
        return 0;
    }
    mv[0] = pred->mv_l0[partition][0];
    mv[1] = pred->mv_l0[partition][1];
    return 1;
}

static int pred_mv_l1(const avc_mb_pred_event_t *pred,
                      unsigned partition,
                      unsigned ref_idx,
                      int16_t mv[2])
{
    if (!pred || pred->partition_count == 0 || partition >= pred->partition_count ||
        (pred->list_mask[partition] & 2u) == 0 || pred->ref_idx_l1[partition] != ref_idx) {
        mv[0] = 0;
        mv[1] = 0;
        return 0;
    }
    mv[0] = pred->mv_l1[partition][0];
    mv[1] = pred->mv_l1[partition][1];
    return 1;
}

static void p_partition_neighbors(avc_p_mb_shape_t shape, unsigned partition,
                                  int *left_current, unsigned *left_partition,
                                  int *top_current, unsigned *top_partition)
{
    *left_current = 0;
    *top_current = 0;
    *left_partition = partition;
    *top_partition = partition;

    if (shape == AVC_P_MB_L0_L0_16X8 && partition == 1) {
        *top_current = 1;
        *top_partition = 0;
    } else if (shape == AVC_P_MB_L0_L0_8X16 && partition == 1) {
        *left_current = 1;
        *left_partition = 0;
    } else if ((shape == AVC_P_MB_8X8 || shape == AVC_P_MB_8X8REF0)) {
        if (partition == 1) {
            *left_current = 1;
            *left_partition = 0;
        } else if (partition == 2) {
            *top_current = 1;
            *top_partition = 0;
        } else if (partition == 3) {
            *left_current = 1;
            *left_partition = 2;
            *top_current = 1;
            *top_partition = 1;
        }
    }
}

static void derive_partition_mv_l0(avc_mb_pred_event_t *pred,
                                   unsigned partition,
                                   avc_p_mb_shape_t shape,
                                   const avc_mb_state_t *left,
                                   const avc_mb_state_t *top)
{
    int left_current;
    int top_current;
    unsigned left_partition;
    unsigned top_partition;
    int16_t a[2] = {0, 0};
    int16_t b[2] = {0, 0};
    int16_t c[2] = {0, 0};
    int have_a;
    int have_b;
    unsigned ref_idx;

    if (!pred || partition >= pred->partition_count || (pred->list_mask[partition] & 1u) == 0) {
        return;
    }

    ref_idx = pred->ref_idx_l0[partition];
    p_partition_neighbors(shape, partition, &left_current, &left_partition,
                          &top_current, &top_partition);
    have_a = left_current ? pred_mv_l0(pred, left_partition, ref_idx, a) :
                            neighbor_mv_l0(left, left_partition, ref_idx, a);
    have_b = top_current ? pred_mv_l0(pred, top_partition, ref_idx, b) :
                           neighbor_mv_l0(top, top_partition, ref_idx, b);
    c[0] = have_b ? b[0] : 0;
    c[1] = have_b ? b[1] : 0;

    if (have_a && !have_b) {
        pred->mv_pred_l0[partition][0] = a[0];
        pred->mv_pred_l0[partition][1] = a[1];
    } else if (!have_a && have_b) {
        pred->mv_pred_l0[partition][0] = b[0];
        pred->mv_pred_l0[partition][1] = b[1];
    } else {
        pred->mv_pred_l0[partition][0] = median_i16(a[0], b[0], c[0]);
        pred->mv_pred_l0[partition][1] = median_i16(a[1], b[1], c[1]);
    }
    pred->mv_l0[partition][0] = (int16_t)(pred->mv_pred_l0[partition][0] + pred->mvd_l0[partition][0]);
    pred->mv_l0[partition][1] = (int16_t)(pred->mv_pred_l0[partition][1] + pred->mvd_l0[partition][1]);
}

static void derive_partition_mv_l1(avc_mb_pred_event_t *pred,
                                   unsigned partition,
                                   avc_p_mb_shape_t shape,
                                   const avc_mb_state_t *left,
                                   const avc_mb_state_t *top)
{
    int left_current;
    int top_current;
    unsigned left_partition;
    unsigned top_partition;
    int16_t a[2] = {0, 0};
    int16_t b[2] = {0, 0};
    int16_t c[2] = {0, 0};
    int have_a;
    int have_b;
    unsigned ref_idx;

    if (!pred || partition >= pred->partition_count || (pred->list_mask[partition] & 2u) == 0) {
        return;
    }

    ref_idx = pred->ref_idx_l1[partition];
    p_partition_neighbors(shape, partition, &left_current, &left_partition,
                          &top_current, &top_partition);
    have_a = left_current ? pred_mv_l1(pred, left_partition, ref_idx, a) :
                            neighbor_mv_l1(left, left_partition, ref_idx, a);
    have_b = top_current ? pred_mv_l1(pred, top_partition, ref_idx, b) :
                           neighbor_mv_l1(top, top_partition, ref_idx, b);
    c[0] = have_b ? b[0] : 0;
    c[1] = have_b ? b[1] : 0;

    if (have_a && !have_b) {
        pred->mv_pred_l1[partition][0] = a[0];
        pred->mv_pred_l1[partition][1] = a[1];
    } else if (!have_a && have_b) {
        pred->mv_pred_l1[partition][0] = b[0];
        pred->mv_pred_l1[partition][1] = b[1];
    } else {
        pred->mv_pred_l1[partition][0] = median_i16(a[0], b[0], c[0]);
        pred->mv_pred_l1[partition][1] = median_i16(a[1], b[1], c[1]);
    }
    pred->mv_l1[partition][0] = (int16_t)(pred->mv_pred_l1[partition][0] + pred->mvd_l1[partition][0]);
    pred->mv_l1[partition][1] = (int16_t)(pred->mv_pred_l1[partition][1] + pred->mvd_l1[partition][1]);
}

static int cabac_parse_intra_mb_pred(avc_cabac_decoder_t *cabac,
                                     uint32_t mb_addr,
                                     avc_i_mb_type_info_t info,
                                     avc_macroblock_callbacks_t callbacks,
                                     void *opaque)
{
    avc_mb_pred_event_t pred;
    unsigned i;

    pred = (avc_mb_pred_event_t){0};
    pred.mb_address = mb_addr;
    pred.kind = info.pred_kind;

    if (info.pred_kind == AVC_MB_PRED_INTRA_4X4 || info.pred_kind == AVC_MB_PRED_INTRA_8X8) {
        unsigned blocks = info.pred_kind == AVC_MB_PRED_INTRA_8X8 ? 4u : 16u;
        for (i = 0; i < blocks; i++) {
            unsigned rem_mode = 0;
            int prev_flag = avc_cabac_decode_prev_intra_pred_mode_flag(cabac);
            if (cabac->error) {
                return 0;
            }
            pred.prev_intra_pred_mode_flag[i] = (uint8_t)prev_flag;
            if (!prev_flag) {
                if (!avc_cabac_decode_rem_intra_pred_mode(cabac, &rem_mode)) {
                    return 0;
                }
                pred.rem_intra_pred_mode[i] = (uint8_t)rem_mode;
            }
        }
    }

    if (info.pred_kind == AVC_MB_PRED_INTRA_4X4 || info.pred_kind == AVC_MB_PRED_INTRA_8X8 ||
        info.pred_kind == AVC_MB_PRED_INTRA_16X16) {
        if (!avc_cabac_decode_intra_chroma_pred_mode(cabac, &pred.intra_chroma_pred_mode)) {
            return 0;
        }
    }

    if (callbacks.on_mb_pred) {
        callbacks.on_mb_pred(opaque, &pred);
    }
    return !cabac->error;
}

static int cabac_parse_p_inter_mb_pred(avc_cabac_decoder_t *cabac,
                                       uint32_t mb_addr,
                                       const avc_slice_header_t *slice,
                                       avc_p_mb_shape_t shape,
                                       const avc_mb_state_t *left,
                                       const avc_mb_state_t *top,
                                       avc_macroblock_callbacks_t callbacks,
                                       void *opaque,
                                       avc_mb_pred_event_t *out_pred)
{
    avc_mb_pred_event_t pred;
    unsigned i;
    unsigned partitions = p_inter_partition_count(shape);
    int force_ref0 = shape == AVC_P_MB_8X8REF0;

    pred = (avc_mb_pred_event_t){0};
    pred.mb_address = mb_addr;
    pred.kind = AVC_MB_PRED_INTER;
    pred.partition_count = partitions;
    for (i = 0; i < partitions; i++) {
        pred.list_mask[i] = 1;
    }

    if (shape == AVC_P_MB_8X8 || shape == AVC_P_MB_8X8REF0) {
        for (i = 0; i < 4; i++) {
            if (!avc_cabac_decode_sub_mb_type_p(cabac, &pred.sub_mb_type[i])) {
                return 0;
            }
        }
    }

    if (!force_ref0 && slice->num_ref_idx_l0_active_minus1 > 0) {
        for (i = 0; i < partitions; i++) {
            int left_current;
            int top_current;
            unsigned left_partition;
            unsigned top_partition;
            unsigned left_ref;
            unsigned top_ref;

            p_partition_neighbors(shape, i, &left_current, &left_partition,
                                  &top_current, &top_partition);
            left_ref = left_current ? pred_ref_idx_l0(&pred, left_partition) :
                                      neighbor_ref_idx_l0(left, left_partition);
            top_ref = top_current ? pred_ref_idx_l0(&pred, top_partition) :
                                    neighbor_ref_idx_l0(top, top_partition);
            if (!avc_cabac_decode_ref_idx_l0(cabac, left_ref != 0, top_ref != 0,
                                             &pred.ref_idx_l0[i])) {
                return 0;
            }
        }
    }

    for (i = 0; i < partitions; i++) {
        int left_current;
        int top_current;
        unsigned left_partition;
        unsigned top_partition;
        unsigned left_mvd_x;
        unsigned top_mvd_x;
        unsigned left_mvd_y;
        unsigned top_mvd_y;

        p_partition_neighbors(shape, i, &left_current, &left_partition,
                              &top_current, &top_partition);
        left_mvd_x = left_current ? pred_abs_mvd_l0(&pred, left_partition, 0) :
                                    neighbor_abs_mvd_l0(left, left_partition, 0);
        top_mvd_x = top_current ? pred_abs_mvd_l0(&pred, top_partition, 0) :
                                  neighbor_abs_mvd_l0(top, top_partition, 0);
        left_mvd_y = left_current ? pred_abs_mvd_l0(&pred, left_partition, 1) :
                                    neighbor_abs_mvd_l0(left, left_partition, 1);
        top_mvd_y = top_current ? pred_abs_mvd_l0(&pred, top_partition, 1) :
                                  neighbor_abs_mvd_l0(top, top_partition, 1);

        if (!avc_cabac_decode_mvd_component(cabac, 40, left_mvd_x, top_mvd_x,
                                            &pred.mvd_l0[i][0])) {
            return 0;
        }
        if (!avc_cabac_decode_mvd_component(cabac, 47, left_mvd_y, top_mvd_y,
                                            &pred.mvd_l0[i][1])) {
            return 0;
        }
        derive_partition_mv_l0(&pred, i, shape, left, top);
    }

    if (out_pred) {
        *out_pred = pred;
    }
    if (callbacks.on_mb_pred) {
        callbacks.on_mb_pred(opaque, &pred);
    }
    return !cabac->error;
}

static int cabac_parse_b_inter_mb_pred(avc_cabac_decoder_t *cabac,
                                       uint32_t mb_addr,
                                       const avc_slice_header_t *slice,
                                       avc_b_mb_type_info_t info,
                                       const avc_mb_state_t *left,
                                       const avc_mb_state_t *top,
                                       avc_macroblock_callbacks_t callbacks,
                                       void *opaque,
                                       avc_mb_pred_event_t *out_pred)
{
    avc_mb_pred_event_t pred;
    unsigned i;

    pred = (avc_mb_pred_event_t){0};
    pred.mb_address = mb_addr;
    pred.kind = AVC_MB_PRED_INTER;
    pred.partition_count = info.partition_count;
    pred.direct_spatial_mv_pred_flag = slice->direct_spatial_mv_pred_flag;

    if (info.shape == AVC_B_MB_DIRECT) {
        pred.list_mask[0] = 0;
        pred.direct_flag[0] = 1;
    } else if (info.shape == AVC_B_MB_8X8) {
        for (i = 0; i < 4; i++) {
            if (!avc_cabac_decode_sub_mb_type_b(cabac, &pred.sub_mb_type[i])) {
                return 0;
            }
            pred.direct_flag[i] = (uint8_t)b_sub_mb_is_direct(pred.sub_mb_type[i]);
            pred.list_mask[i] = b_sub_mb_list_mask(pred.sub_mb_type[i]);
        }
    } else {
        for (i = 0; i < info.partition_count; i++) {
            pred.list_mask[i] = info.list_mask[i];
        }
    }

    if (slice->num_ref_idx_l0_active_minus1 > 0) {
        for (i = 0; i < pred.partition_count; i++) {
            int left_current;
            int top_current;
            unsigned left_partition;
            unsigned top_partition;
            unsigned left_ref;
            unsigned top_ref;

            if ((pred.list_mask[i] & 1u) == 0) {
                continue;
            }
            p_partition_neighbors(info.shape == AVC_B_MB_8X8 ? AVC_P_MB_8X8 : AVC_P_MB_L0_L0_16X8,
                                  i, &left_current, &left_partition,
                                  &top_current, &top_partition);
            left_ref = left_current ? pred_ref_idx_l0(&pred, left_partition) :
                                      neighbor_ref_idx_l0(left, left_partition);
            top_ref = top_current ? pred_ref_idx_l0(&pred, top_partition) :
                                    neighbor_ref_idx_l0(top, top_partition);
            if (!avc_cabac_decode_ref_idx_l0(cabac, left_ref != 0, top_ref != 0,
                                             &pred.ref_idx_l0[i])) {
                return 0;
            }
        }
    }
    if (slice->num_ref_idx_l1_active_minus1 > 0) {
        for (i = 0; i < pred.partition_count; i++) {
            int left_current;
            int top_current;
            unsigned left_partition;
            unsigned top_partition;
            unsigned left_ref;
            unsigned top_ref;

            if ((pred.list_mask[i] & 2u) == 0) {
                continue;
            }
            p_partition_neighbors(info.shape == AVC_B_MB_8X8 ? AVC_P_MB_8X8 : AVC_P_MB_L0_L0_16X8,
                                  i, &left_current, &left_partition,
                                  &top_current, &top_partition);
            left_ref = left_current ? pred_ref_idx_l1(&pred, left_partition) :
                                      neighbor_ref_idx_l1(left, left_partition);
            top_ref = top_current ? pred_ref_idx_l1(&pred, top_partition) :
                                    neighbor_ref_idx_l1(top, top_partition);
            if (!avc_cabac_decode_ref_idx_l1(cabac, left_ref != 0, top_ref != 0,
                                             &pred.ref_idx_l1[i])) {
                return 0;
            }
        }
    }

    for (i = 0; i < pred.partition_count; i++) {
        int left_current;
        int top_current;
        unsigned left_partition;
        unsigned top_partition;
        avc_p_mb_shape_t neighbor_shape = info.shape == AVC_B_MB_8X8 ? AVC_P_MB_8X8 : AVC_P_MB_L0_L0_16X8;

        p_partition_neighbors(neighbor_shape, i, &left_current, &left_partition,
                              &top_current, &top_partition);
        if (pred.list_mask[i] & 1u) {
            unsigned left_x = left_current ? pred_abs_mvd_l0(&pred, left_partition, 0) :
                                             neighbor_abs_mvd_l0(left, left_partition, 0);
            unsigned top_x = top_current ? pred_abs_mvd_l0(&pred, top_partition, 0) :
                                           neighbor_abs_mvd_l0(top, top_partition, 0);
            unsigned left_y = left_current ? pred_abs_mvd_l0(&pred, left_partition, 1) :
                                             neighbor_abs_mvd_l0(left, left_partition, 1);
            unsigned top_y = top_current ? pred_abs_mvd_l0(&pred, top_partition, 1) :
                                           neighbor_abs_mvd_l0(top, top_partition, 1);
            if (!avc_cabac_decode_mvd_component(cabac, 40, left_x, top_x, &pred.mvd_l0[i][0]) ||
                !avc_cabac_decode_mvd_component(cabac, 47, left_y, top_y, &pred.mvd_l0[i][1])) {
                return 0;
            }
            derive_partition_mv_l0(&pred, i, neighbor_shape, left, top);
        }
        if (pred.list_mask[i] & 2u) {
            unsigned left_x = left_current ? pred_abs_mvd_l1(&pred, left_partition, 0) :
                                             neighbor_abs_mvd_l1(left, left_partition, 0);
            unsigned top_x = top_current ? pred_abs_mvd_l1(&pred, top_partition, 0) :
                                           neighbor_abs_mvd_l1(top, top_partition, 0);
            unsigned left_y = left_current ? pred_abs_mvd_l1(&pred, left_partition, 1) :
                                             neighbor_abs_mvd_l1(left, left_partition, 1);
            unsigned top_y = top_current ? pred_abs_mvd_l1(&pred, top_partition, 1) :
                                           neighbor_abs_mvd_l1(top, top_partition, 1);
            if (!avc_cabac_decode_mvd_component(cabac, 40, left_x, top_x, &pred.mvd_l1[i][0]) ||
                !avc_cabac_decode_mvd_component(cabac, 47, left_y, top_y, &pred.mvd_l1[i][1])) {
                return 0;
            }
            derive_partition_mv_l1(&pred, i, neighbor_shape, left, top);
        }
    }

    if (out_pred) {
        *out_pred = pred;
    }
    if (callbacks.on_mb_pred) {
        callbacks.on_mb_pred(opaque, &pred);
    }
    return !cabac->error;
}

static void emit_b_direct_pred(uint32_t mb_addr,
                               const avc_slice_header_t *slice,
                               avc_macroblock_callbacks_t callbacks,
                               void *opaque,
                               avc_mb_pred_event_t *out_pred)
{
    avc_mb_pred_event_t pred;

    pred = (avc_mb_pred_event_t){0};
    pred.mb_address = mb_addr;
    pred.kind = AVC_MB_PRED_INTER;
    pred.partition_count = 1;
    pred.direct_flag[0] = 1;
    pred.direct_spatial_mv_pred_flag = slice->direct_spatial_mv_pred_flag;
    if (out_pred) {
        *out_pred = pred;
    }
    if (callbacks.on_mb_pred) {
        callbacks.on_mb_pred(opaque, &pred);
    }
}

static void emit_p_skip_pred(uint32_t mb_addr,
                             const avc_mb_state_t *left,
                             const avc_mb_state_t *top,
                             avc_macroblock_callbacks_t callbacks,
                             void *opaque,
                             avc_mb_pred_event_t *out_pred)
{
    avc_mb_pred_event_t pred;

    pred = (avc_mb_pred_event_t){0};
    pred.mb_address = mb_addr;
    pred.kind = AVC_MB_PRED_INTER;
    pred.partition_count = 1;
    pred.list_mask[0] = 1;
    pred.ref_idx_l0[0] = 0;
    {
        int16_t mv_a[2] = {0, 0};
        int16_t mv_b[2] = {0, 0};
        int have_a = neighbor_mv_l0(left, 0, 0, mv_a);
        int have_b = neighbor_mv_l0(top, 0, 0, mv_b);

        if (!have_a || !have_b ||
            (mv_a[0] == 0 && mv_a[1] == 0) ||
            (mv_b[0] == 0 && mv_b[1] == 0)) {
            pred.mv_pred_l0[0][0] = 0;
            pred.mv_pred_l0[0][1] = 0;
            pred.mv_l0[0][0] = 0;
            pred.mv_l0[0][1] = 0;
        } else {
            derive_partition_mv_l0(&pred, 0, AVC_P_MB_L0_16X16, left, top);
        }
    }
    if (out_pred) {
        *out_pred = pred;
    }
    if (callbacks.on_mb_pred) {
        callbacks.on_mb_pred(opaque, &pred);
    }
}

static const avc_cabac_residual_plan_t *residual_plan(avc_residual_kind_t kind)
{
    unsigned i;

    for (i = 0; i < sizeof(residual_plans) / sizeof(residual_plans[0]); i++) {
        if (residual_plans[i].kind == kind) {
            return &residual_plans[i];
        }
    }
    return NULL;
}

static int cabac_emit_residual_block(avc_cabac_decoder_t *cabac,
                                     uint32_t mb_addr,
                                     avc_residual_kind_t kind,
                                     unsigned block_index,
                                     unsigned max_coeff_override,
                                     int left_coded,
                                     int top_coded,
                                     unsigned chroma_format,
                                     avc_mb_state_t *curr,
                                     avc_macroblock_callbacks_t callbacks,
                                     void *opaque)
{
    const avc_cabac_residual_plan_t *plan = residual_plan(kind);
    avc_residual_event_t residual;
    unsigned max_coeff;

    if (!plan) {
        cabac->error = 1;
        return 0;
    }

    residual = (avc_residual_event_t){0};
    residual.mb_address = mb_addr;
    residual.block_kind = kind;
    residual.block_index = block_index;
    residual.entropy = AVC_MB_ENTROPY_CABAC;
    max_coeff = max_coeff_override ? max_coeff_override : plan->max_coeff;

    if (!avc_cabac_decode_residual_block(cabac, max_coeff,
                                         plan->coded_ctx_base,
                                         plan->sig_ctx_base,
                                         plan->last_ctx_base,
                                         plan->level_ctx_base,
                                         left_coded, top_coded,
                                         &residual.cabac_block)) {
        notef(callbacks, opaque,
              "CABAC residual parse failed mb=%u kind=%u block=%u max_coeff=%u bit=%zu",
              mb_addr, (unsigned)kind, block_index, max_coeff, cabac->bit_pos);
        return 0;
    }
    store_residual_nonzero(curr, kind, block_index, residual.cabac_block.total_coeff,
                           chroma_format);
    if (callbacks.on_residual) {
        callbacks.on_residual(opaque, &residual);
    }
    return !cabac->error;
}

static int cabac_emit_luma_residuals(avc_cabac_decoder_t *cabac,
                                     uint32_t mb_addr,
                                     avc_mb_pred_kind_t pred_kind,
                                     uint32_t cbp_luma,
                                     int transform_size_8x8_flag,
                                     const avc_mb_state_t *left,
                                     const avc_mb_state_t *top,
                                     avc_mb_state_t *curr,
                                     avc_macroblock_callbacks_t callbacks,
                                     void *opaque)
{
    unsigned group;
    int left_coded;
    int top_coded;

    if (pred_kind == AVC_MB_PRED_INTRA_16X16) {
        cabac_luma16_dc_cbf_neighbors(left, top, &left_coded, &top_coded);
        if (!cabac_emit_residual_block(cabac, mb_addr, AVC_RESIDUAL_LUMA_16X16_DC,
                                       0, 0, left_coded, top_coded, 0, curr,
                                       callbacks, opaque)) {
            return 0;
        }
        if (!cbp_luma) {
            return 1;
        }
        for (group = 0; group < 16; group++) {
            cabac_luma_cbf_neighbors(curr, left, top, group, 0,
                                     &left_coded, &top_coded);
            if (!cabac_emit_residual_block(cabac, mb_addr, AVC_RESIDUAL_LUMA_16X16_AC,
                                           group, 0, left_coded, top_coded, 0, curr,
                                           callbacks, opaque)) {
                return 0;
            }
        }
        return 1;
    }

    for (group = 0; group < 4; group++) {
        unsigned sub;

        if (((cbp_luma >> group) & 1u) == 0) {
            continue;
        }
        if (transform_size_8x8_flag || pred_kind == AVC_MB_PRED_INTRA_8X8) {
            cabac_luma_cbf_neighbors(curr, left, top, group, 1,
                                     &left_coded, &top_coded);
            if (!cabac_emit_residual_block(cabac, mb_addr, AVC_RESIDUAL_LUMA_8X8,
                                           group, 0, left_coded, top_coded, 0, curr,
                                           callbacks, opaque)) {
                return 0;
            }
        } else {
            for (sub = 0; sub < 4; sub++) {
                unsigned block_index = group * 4u + sub;
                cabac_luma_cbf_neighbors(curr, left, top, block_index, 0,
                                         &left_coded, &top_coded);
                if (!cabac_emit_residual_block(cabac, mb_addr, AVC_RESIDUAL_LUMA_4X4,
                                               block_index, 0, left_coded, top_coded, 0, curr,
                                               callbacks, opaque)) {
                    return 0;
                }
            }
        }
    }
    return !cabac->error;
}

static unsigned chroma_array_type(const avc_sps_t *sps)
{
    return sps->chroma_format_idc;
}

static unsigned chroma_dc_coeffs(unsigned chroma_format_idc)
{
    if (chroma_format_idc == 1) {
        return 4;
    }
    if (chroma_format_idc == 2) {
        return 8;
    }
    if (chroma_format_idc == 3) {
        return 16;
    }
    return 0;
}

static unsigned chroma_ac_blocks_per_component(unsigned chroma_format_idc)
{
    if (chroma_format_idc == 1) {
        return 4;
    }
    if (chroma_format_idc == 2) {
        return 8;
    }
    if (chroma_format_idc == 3) {
        return 16;
    }
    return 0;
}

static int cabac_emit_chroma_residuals(avc_cabac_decoder_t *cabac,
                                       uint32_t mb_addr,
                                       const avc_sps_t *sps,
                                       uint32_t cbp_chroma,
                                       const avc_mb_state_t *left,
                                       const avc_mb_state_t *top,
                                       avc_mb_state_t *curr,
                                       avc_macroblock_callbacks_t callbacks,
                                       void *opaque)
{
    unsigned chroma_format = chroma_array_type(sps);
    unsigned dc_coeffs = chroma_dc_coeffs(chroma_format);
    unsigned ac_blocks = chroma_ac_blocks_per_component(chroma_format);
    unsigned component;

    if (cbp_chroma == 0 || chroma_format == 0) {
        return 1;
    }

    for (component = 0; component < 2; component++) {
        int left_coded;
        int top_coded;
        cabac_chroma_dc_cbf_neighbors(left, top, component, &left_coded, &top_coded);
        if (!cabac_emit_residual_block(cabac, mb_addr, AVC_RESIDUAL_CHROMA_DC,
                                       component, dc_coeffs, left_coded, top_coded,
                                       chroma_format, curr, callbacks, opaque)) {
            return 0;
        }
    }

    if (cbp_chroma != 2) {
        return !cabac->error;
    }

    for (component = 0; component < 2; component++) {
        unsigned block;
        for (block = 0; block < ac_blocks; block++) {
            unsigned block_index = component * ac_blocks + block;
            int left_coded;
            int top_coded;
            cabac_chroma_ac_cbf_neighbors(curr, left, top, chroma_format,
                                          component, block, &left_coded, &top_coded);
            if (!cabac_emit_residual_block(cabac, mb_addr, AVC_RESIDUAL_CHROMA_AC,
                                           block_index, 0, left_coded, top_coded,
                                           chroma_format, curr, callbacks, opaque)) {
                return 0;
            }
        }
    }
    return !cabac->error;
}

static int cabac_consume_ipcm(avc_cabac_decoder_t *cabac,
                              const avc_sps_t *sps,
                              avc_macroblock_event_t *event)
{
    uint32_t i;
    unsigned chroma_format = chroma_array_type(sps);
    unsigned luma_bits = sps->bit_depth_luma_minus8 + 8u;
    unsigned chroma_bits = sps->bit_depth_chroma_minus8 + 8u;
    uint32_t chroma_samples_per_component = 0;

    if (luma_bits > 32 || chroma_bits > 32) {
        cabac->error = 1;
        return 0;
    }

    if (chroma_format == 1) {
        chroma_samples_per_component = 64;
    } else if (chroma_format == 2) {
        chroma_samples_per_component = 128;
    } else if (chroma_format == 3) {
        chroma_samples_per_component = 256;
    }

    avc_cabac_byte_align(cabac);
    if (cabac->error) {
        return 0;
    }

    event->pcm_sample_bits_luma = (int)luma_bits;
    event->pcm_sample_bits_chroma = chroma_format == 0 ? 0 : (int)chroma_bits;
    event->pcm_luma_samples = 256;
    event->pcm_chroma_samples = chroma_samples_per_component * 2u;

    for (i = 0; i < event->pcm_luma_samples; i++) {
        avc_cabac_read_pcm_bits(cabac, luma_bits);
    }
    for (i = 0; i < event->pcm_chroma_samples; i++) {
        avc_cabac_read_pcm_bits(cabac, chroma_bits);
    }
    return !cabac->error;
}

static int cavlc_coded_block_pattern(avc_bitreader_t *br, int intra,
                                     uint32_t *cbp_luma,
                                     uint32_t *cbp_chroma)
{
    static const uint8_t inter_map[48] = {
        0,16,1,2,4,8,32,3,5,10,12,15,47,7,11,13,
        14,6,9,31,35,37,42,44,33,34,36,40,39,43,45,46,
        17,18,20,24,19,21,26,28,23,27,29,30,22,25,38,41
    };
    static const uint8_t intra_map[48] = {
        47,31,15,0,23,27,29,30,7,11,13,14,39,43,45,46,
        16,3,5,10,12,19,21,26,28,35,37,42,44,1,2,4,
        8,17,18,20,24,6,9,22,25,32,33,34,36,40,38,41
    };
    uint32_t code = avc_br_read_ue(br);
    uint32_t cbp;

    if (code >= 48) {
        br->error = 1;
        return 0;
    }
    cbp = intra ? intra_map[code] : inter_map[code];
    *cbp_luma = cbp & 15u;
    *cbp_chroma = cbp >> 4;
    return !br->error;
}

static int cavlc_parse_intra_mb_pred(avc_bitreader_t *br,
                                     uint32_t mb_addr,
                                     avc_i_mb_type_info_t info,
                                     avc_macroblock_callbacks_t callbacks,
                                     void *opaque)
{
    avc_mb_pred_event_t pred;
    unsigned i;

    pred = (avc_mb_pred_event_t){0};
    pred.mb_address = mb_addr;
    pred.kind = info.pred_kind;

    if (info.pred_kind == AVC_MB_PRED_INTRA_4X4 || info.pred_kind == AVC_MB_PRED_INTRA_8X8) {
        unsigned blocks = info.pred_kind == AVC_MB_PRED_INTRA_8X8 ? 4u : 16u;
        for (i = 0; i < blocks; i++) {
            pred.prev_intra_pred_mode_flag[i] = (uint8_t)avc_br_read_bit(br);
            if (!pred.prev_intra_pred_mode_flag[i]) {
                pred.rem_intra_pred_mode[i] = (uint8_t)avc_br_read_bits(br, 3);
            }
        }
    }
    if (info.pred_kind == AVC_MB_PRED_INTRA_4X4 || info.pred_kind == AVC_MB_PRED_INTRA_8X8 ||
        info.pred_kind == AVC_MB_PRED_INTRA_16X16) {
        pred.intra_chroma_pred_mode = avc_br_read_ue(br);
        if (pred.intra_chroma_pred_mode > 3) {
            br->error = 1;
            return 0;
        }
    }
    if (callbacks.on_mb_pred) {
        callbacks.on_mb_pred(opaque, &pred);
    }
    return !br->error;
}

static int cavlc_parse_p_inter_mb_pred(avc_bitreader_t *br,
                                       uint32_t mb_addr,
                                       const avc_slice_header_t *slice,
                                       avc_p_mb_shape_t shape,
                                       const avc_mb_state_t *left,
                                       const avc_mb_state_t *top,
                                       avc_macroblock_callbacks_t callbacks,
                                       void *opaque,
                                       avc_mb_pred_event_t *out_pred)
{
    avc_mb_pred_event_t pred;
    unsigned i;
    unsigned partitions = p_inter_partition_count(shape);
    int force_ref0 = shape == AVC_P_MB_8X8REF0;

    pred = (avc_mb_pred_event_t){0};
    pred.mb_address = mb_addr;
    pred.kind = AVC_MB_PRED_INTER;
    pred.partition_count = partitions;
    for (i = 0; i < partitions; i++) {
        pred.list_mask[i] = 1;
    }

    if (shape == AVC_P_MB_8X8 || shape == AVC_P_MB_8X8REF0) {
        for (i = 0; i < 4; i++) {
            pred.sub_mb_type[i] = avc_br_read_ue(br);
        }
    }
    if (!force_ref0 && slice->num_ref_idx_l0_active_minus1 > 0) {
        for (i = 0; i < partitions; i++) {
            pred.ref_idx_l0[i] = avc_br_read_ue(br);
        }
    }
    for (i = 0; i < partitions; i++) {
        pred.mvd_l0[i][0] = (int16_t)avc_br_read_se(br);
        pred.mvd_l0[i][1] = (int16_t)avc_br_read_se(br);
        derive_partition_mv_l0(&pred, i, shape, left, top);
    }
    if (out_pred) {
        *out_pred = pred;
    }
    if (callbacks.on_mb_pred) {
        callbacks.on_mb_pred(opaque, &pred);
    }
    return !br->error;
}

static int cavlc_parse_b_inter_mb_pred(avc_bitreader_t *br,
                                       uint32_t mb_addr,
                                       const avc_slice_header_t *slice,
                                       avc_b_mb_type_info_t info,
                                       const avc_mb_state_t *left,
                                       const avc_mb_state_t *top,
                                       avc_macroblock_callbacks_t callbacks,
                                       void *opaque,
                                       avc_mb_pred_event_t *out_pred)
{
    avc_mb_pred_event_t pred;
    unsigned i;

    pred = (avc_mb_pred_event_t){0};
    pred.mb_address = mb_addr;
    pred.kind = AVC_MB_PRED_INTER;
    pred.partition_count = info.partition_count;
    pred.direct_spatial_mv_pred_flag = slice->direct_spatial_mv_pred_flag;

    if (info.shape == AVC_B_MB_DIRECT) {
        pred.direct_flag[0] = 1;
    } else if (info.shape == AVC_B_MB_8X8) {
        for (i = 0; i < 4; i++) {
            pred.sub_mb_type[i] = avc_br_read_ue(br);
            pred.direct_flag[i] = (uint8_t)b_sub_mb_is_direct(pred.sub_mb_type[i]);
            pred.list_mask[i] = b_sub_mb_list_mask(pred.sub_mb_type[i]);
        }
    } else {
        for (i = 0; i < info.partition_count; i++) {
            pred.list_mask[i] = info.list_mask[i];
        }
    }

    if (slice->num_ref_idx_l0_active_minus1 > 0) {
        for (i = 0; i < pred.partition_count; i++) {
            if (pred.list_mask[i] & 1u) {
                pred.ref_idx_l0[i] = avc_br_read_ue(br);
            }
        }
    }
    if (slice->num_ref_idx_l1_active_minus1 > 0) {
        for (i = 0; i < pred.partition_count; i++) {
            if (pred.list_mask[i] & 2u) {
                pred.ref_idx_l1[i] = avc_br_read_ue(br);
            }
        }
    }

    for (i = 0; i < pred.partition_count; i++) {
        avc_p_mb_shape_t neighbor_shape = info.shape == AVC_B_MB_8X8 ? AVC_P_MB_8X8 : AVC_P_MB_L0_L0_16X8;
        if (pred.list_mask[i] & 1u) {
            pred.mvd_l0[i][0] = (int16_t)avc_br_read_se(br);
            pred.mvd_l0[i][1] = (int16_t)avc_br_read_se(br);
            derive_partition_mv_l0(&pred, i, neighbor_shape, left, top);
        }
        if (pred.list_mask[i] & 2u) {
            pred.mvd_l1[i][0] = (int16_t)avc_br_read_se(br);
            pred.mvd_l1[i][1] = (int16_t)avc_br_read_se(br);
            derive_partition_mv_l1(&pred, i, neighbor_shape, left, top);
        }
    }

    if (out_pred) {
        *out_pred = pred;
    }
    if (callbacks.on_mb_pred) {
        callbacks.on_mb_pred(opaque, &pred);
    }
    return !br->error;
}

static int cavlc_emit_residual_block(avc_bitreader_t *br,
                                     uint32_t mb_addr,
                                     avc_residual_kind_t kind,
                                     unsigned block_index,
                                     unsigned max_coeff,
                                     int nC,
                                     unsigned chroma_format,
                                     avc_mb_state_t *curr,
                                     avc_macroblock_callbacks_t callbacks,
                                     void *opaque)
{
    avc_residual_event_t residual;
    avc_cavlc_callbacks_t cavlc_callbacks = {0};

    residual = (avc_residual_event_t){0};
    residual.mb_address = mb_addr;
    residual.block_kind = kind;
    residual.block_index = block_index;
    residual.entropy = AVC_MB_ENTROPY_CAVLC;

    if (!avc_cavlc_read_residual_block(br, nC, max_coeff, &residual.block,
                                       cavlc_callbacks, NULL)) {
        notef(callbacks, opaque,
              "CAVLC residual parse failed mb=%u kind=%u block=%u max_coeff=%u nC=%d bit=%zu",
              mb_addr, (unsigned)kind, block_index, max_coeff, nC, br->bit_pos);
        return 0;
    }
    store_residual_nonzero(curr, kind, block_index, residual.block.total_coeff,
                           chroma_format);
    if (callbacks.on_residual) {
        callbacks.on_residual(opaque, &residual);
    }
    return !br->error;
}

static int cavlc_emit_luma_residuals(avc_bitreader_t *br,
                                     uint32_t mb_addr,
                                     avc_mb_pred_kind_t pred_kind,
                                     uint32_t cbp_luma,
                                     int transform_size_8x8_flag,
                                     const avc_mb_state_t *left,
                                     const avc_mb_state_t *top,
                                     avc_mb_state_t *curr,
                                     avc_macroblock_callbacks_t callbacks,
                                     void *opaque)
{
    unsigned group;

    if (pred_kind == AVC_MB_PRED_INTRA_16X16) {
        if (!cavlc_emit_residual_block(br, mb_addr, AVC_RESIDUAL_LUMA_16X16_DC,
                                       0, 16, 0, 0, curr, callbacks, opaque)) {
            return 0;
        }
        if (!cbp_luma) {
            return 1;
        }
        for (group = 0; group < 16; group++) {
            if (!cavlc_emit_residual_block(br, mb_addr, AVC_RESIDUAL_LUMA_16X16_AC,
                                           group, 15,
                                           cavlc_luma_nC(curr, left, top, group),
                                           0, curr, callbacks, opaque)) {
                return 0;
            }
        }
        return 1;
    }

    for (group = 0; group < 4; group++) {
        unsigned sub;
        if (((cbp_luma >> group) & 1u) == 0) {
            continue;
        }
        if (transform_size_8x8_flag || pred_kind == AVC_MB_PRED_INTRA_8X8) {
            for (sub = 0; sub < 4; sub++) {
                unsigned block_index = group * 4u + sub;
                if (!cavlc_emit_residual_block(br, mb_addr, AVC_RESIDUAL_LUMA_8X8,
                                               block_index, 16,
                                               cavlc_luma_nC(curr, left, top, block_index),
                                               0, curr, callbacks, opaque)) {
                    return 0;
                }
            }
        } else {
            for (sub = 0; sub < 4; sub++) {
                unsigned block_index = group * 4u + sub;
                if (!cavlc_emit_residual_block(br, mb_addr, AVC_RESIDUAL_LUMA_4X4,
                                               block_index, 16,
                                               cavlc_luma_nC(curr, left, top, block_index),
                                               0, curr, callbacks, opaque)) {
                    return 0;
                }
            }
        }
    }
    return !br->error;
}

static int cavlc_emit_chroma_residuals(avc_bitreader_t *br,
                                       uint32_t mb_addr,
                                       const avc_sps_t *sps,
                                       uint32_t cbp_chroma,
                                       const avc_mb_state_t *left,
                                       const avc_mb_state_t *top,
                                       avc_mb_state_t *curr,
                                       avc_macroblock_callbacks_t callbacks,
                                       void *opaque)
{
    unsigned chroma_format = chroma_array_type(sps);
    unsigned dc_coeffs = chroma_dc_coeffs(chroma_format);
    unsigned ac_blocks = chroma_ac_blocks_per_component(chroma_format);
    unsigned component;

    if (cbp_chroma == 0 || chroma_format == 0) {
        return 1;
    }
    for (component = 0; component < 2; component++) {
        if (!cavlc_emit_residual_block(br, mb_addr, AVC_RESIDUAL_CHROMA_DC,
                                       component, dc_coeffs, 0, chroma_format,
                                       curr, callbacks, opaque)) {
            return 0;
        }
    }
    if (cbp_chroma != 2) {
        return !br->error;
    }
    for (component = 0; component < 2; component++) {
        unsigned block;
        for (block = 0; block < ac_blocks; block++) {
            if (!cavlc_emit_residual_block(br, mb_addr, AVC_RESIDUAL_CHROMA_AC,
                                           component * ac_blocks + block, 15,
                                           cavlc_chroma_ac_nC(curr, left, top,
                                                             chroma_format,
                                                             component, block),
                                           chroma_format, curr,
                                           callbacks, opaque)) {
                return 0;
            }
        }
    }
    return !br->error;
}

static int cavlc_consume_ipcm(avc_bitreader_t *br,
                              const avc_sps_t *sps,
                              avc_macroblock_event_t *event)
{
    uint32_t i;
    unsigned chroma_format = chroma_array_type(sps);
    unsigned luma_bits = sps->bit_depth_luma_minus8 + 8u;
    unsigned chroma_bits = sps->bit_depth_chroma_minus8 + 8u;
    uint32_t chroma_samples_per_component = 0;

    if (luma_bits > 32 || chroma_bits > 32) {
        br->error = 1;
        return 0;
    }
    while ((br->bit_pos & 7u) != 0) {
        if (avc_br_read_bit(br) != 0) {
            br->error = 1;
            return 0;
        }
    }

    if (chroma_format == 1) {
        chroma_samples_per_component = 64;
    } else if (chroma_format == 2) {
        chroma_samples_per_component = 128;
    } else if (chroma_format == 3) {
        chroma_samples_per_component = 256;
    }

    event->pcm_sample_bits_luma = (int)luma_bits;
    event->pcm_sample_bits_chroma = chroma_format == 0 ? 0 : (int)chroma_bits;
    event->pcm_luma_samples = 256;
    event->pcm_chroma_samples = chroma_samples_per_component * 2u;

    for (i = 0; i < event->pcm_luma_samples; i++) {
        avc_br_read_bits(br, luma_bits);
    }
    for (i = 0; i < event->pcm_chroma_samples; i++) {
        avc_br_read_bits(br, chroma_bits);
    }
    return !br->error;
}

static int parse_cavlc_slice_data(avc_bitreader_t *br,
                                  const avc_slice_header_t *slice,
                                  const avc_pps_t *pps,
                                  const avc_sps_t *sps,
                                  avc_macroblock_callbacks_t callbacks,
                                  void *opaque,
                                  avc_slice_data_summary_t *summary)
{
    uint32_t mb_addr = slice->first_mb_in_slice;
    uint32_t max_mbs = pic_size_in_mbs(sps);
    uint32_t width = pic_width_in_mbs(sps);
    avc_mb_state_t *states = (avc_mb_state_t *)calloc(max_mbs ? max_mbs : 1u, sizeof(*states));

    if (!states) {
        return 0;
    }

    while (mb_addr < max_mbs && avc_br_more_rbsp_data(br)) {
        avc_macroblock_event_t event;
        const avc_mb_state_t *left;
        const avc_mb_state_t *top;
        avc_mb_state_t *curr;
        avc_mb_pred_event_t pred_event;
        avc_mb_pred_event_t *pred_for_state = NULL;

        event = (avc_macroblock_event_t){0};
        pred_event = (avc_mb_pred_event_t){0};
        event.address = mb_addr;
        event.entropy = AVC_MB_ENTROPY_CAVLC;
        neighbor_states(states, max_mbs, width, mb_addr, &left, &top);
        curr = state_for(states, max_mbs, mb_addr);

        if (slice->slice_kind == AVC_SLICE_P || slice->slice_kind == AVC_SLICE_SP ||
            slice->slice_kind == AVC_SLICE_B) {
            event.mb_skip_run = avc_br_read_ue(br);
            if (br->error) {
                free(states);
                return 0;
            }
            while (event.mb_skip_run > 0 && mb_addr < max_mbs) {
                avc_macroblock_event_t skipped = event;
                avc_mb_pred_event_t skipped_pred;
                avc_mb_pred_event_t *skipped_pred_ptr = NULL;

                skipped.address = mb_addr++;
                skipped.skipped = 1;
                if (slice->slice_kind == AVC_SLICE_B) {
                    skipped_pred = (avc_mb_pred_event_t){0};
                    skipped_pred.mb_address = skipped.address;
                    skipped_pred.kind = AVC_MB_PRED_INTER;
                    skipped_pred.partition_count = 1;
                    skipped_pred.direct_flag[0] = 1;
                    skipped_pred.direct_spatial_mv_pred_flag = slice->direct_spatial_mv_pred_flag;
                    skipped_pred_ptr = &skipped_pred;
                    if (callbacks.on_mb_pred) {
                        callbacks.on_mb_pred(opaque, &skipped_pred);
                    }
                } else {
                    const avc_mb_state_t *skip_left;
                    const avc_mb_state_t *skip_top;

                    neighbor_states(states, max_mbs, width, skipped.address,
                                    &skip_left, &skip_top);
                    emit_p_skip_pred(skipped.address, skip_left, skip_top,
                                     callbacks, opaque, &skipped_pred);
                    skipped_pred_ptr = &skipped_pred;
                }
                if (callbacks.on_macroblock) {
                    callbacks.on_macroblock(opaque, &skipped);
                }
                save_mb_state(states, max_mbs, &skipped, skipped_pred_ptr);
                summary->macroblocks_seen++;
                event.mb_skip_run--;
            }
            if (mb_addr >= max_mbs || !avc_br_more_rbsp_data(br)) {
                break;
            }
            event.address = mb_addr;
            neighbor_states(states, max_mbs, width, mb_addr, &left, &top);
            curr = state_for(states, max_mbs, mb_addr);
        }

        event.mb_type = avc_br_read_ue(br);
        if (br->error) {
            free(states);
            return 0;
        }
        if (slice->slice_kind == AVC_SLICE_I || slice->slice_kind == AVC_SLICE_SI) {
            avc_i_mb_type_info_t i_info = classify_i_mb_type(event.mb_type);

            if (i_info.is_pcm) {
                if (!cavlc_consume_ipcm(br, sps, &event)) {
                    free(states);
                    return 0;
                }
                cavlc_mark_ipcm_nonzero(curr, sps);
                if (callbacks.on_macroblock) {
                    callbacks.on_macroblock(opaque, &event);
                }
                save_mb_state(states, max_mbs, &event, NULL);
                summary->macroblocks_seen++;
                mb_addr++;
                continue;
            }
            if (i_info.pred_kind == AVC_MB_PRED_UNKNOWN) {
                note(callbacks, opaque, "unsupported I-slice CAVLC macroblock type");
                free(states);
                return 0;
            }
            if (pps->transform_8x8_mode_flag && i_info.pred_kind == AVC_MB_PRED_INTRA_4X4) {
                event.transform_size_8x8_flag = (int)avc_br_read_bit(br);
                if (br->error) {
                    free(states);
                    return 0;
                }
                if (event.transform_size_8x8_flag) {
                    i_info.pred_kind = AVC_MB_PRED_INTRA_8X8;
                }
            }
            if (!cavlc_parse_intra_mb_pred(br, mb_addr, i_info, callbacks, opaque)) {
                free(states);
                return 0;
            }
            if (i_info.pred_kind == AVC_MB_PRED_INTRA_4X4 || i_info.pred_kind == AVC_MB_PRED_INTRA_8X8) {
                if (!cavlc_coded_block_pattern(br, 1, &event.coded_block_pattern_luma,
                                               &event.coded_block_pattern_chroma)) {
                    free(states);
                    return 0;
                }
                if (sps->chroma_format_idc == 0) {
                    event.coded_block_pattern_chroma = 0;
                }
            } else {
                event.coded_block_pattern_luma = i_info.cbp_luma;
                event.coded_block_pattern_chroma = i_info.cbp_chroma;
            }
            if (event.coded_block_pattern_luma || event.coded_block_pattern_chroma ||
                i_info.pred_kind == AVC_MB_PRED_INTRA_16X16) {
                event.mb_qp_delta = avc_br_read_se(br);
            }
            if (callbacks.on_macroblock) {
                callbacks.on_macroblock(opaque, &event);
            }
            summary->macroblocks_seen++;
            if (!cavlc_emit_luma_residuals(br, mb_addr, i_info.pred_kind,
                                           event.coded_block_pattern_luma,
                                           event.transform_size_8x8_flag,
                                           left, top, curr,
                                           callbacks, opaque) ||
                !cavlc_emit_chroma_residuals(br, mb_addr, sps,
                                             event.coded_block_pattern_chroma,
                                             left, top, curr,
                                             callbacks, opaque)) {
                free(states);
                return 0;
            }
            save_mb_state(states, max_mbs, &event, NULL);
        } else if (slice->slice_kind == AVC_SLICE_P || slice->slice_kind == AVC_SLICE_SP) {
            avc_p_mb_type_info_t p_info = classify_p_mb_type(event.mb_type);
            avc_i_mb_type_info_t i_info = p_info.intra;

            if (p_info.shape == AVC_P_MB_UNKNOWN) {
                note(callbacks, opaque, "unsupported P-slice CAVLC macroblock type");
                free(states);
                return 0;
            }
            if (p_info.shape == AVC_P_MB_INTRA) {
                if (i_info.is_pcm || i_info.pred_kind == AVC_MB_PRED_UNKNOWN) {
                    if (i_info.is_pcm) {
                        if (!cavlc_consume_ipcm(br, sps, &event)) {
                            free(states);
                            return 0;
                        }
                        cavlc_mark_ipcm_nonzero(curr, sps);
                        if (callbacks.on_macroblock) {
                            callbacks.on_macroblock(opaque, &event);
                        }
                        save_mb_state(states, max_mbs, &event, NULL);
                        summary->macroblocks_seen++;
                        mb_addr++;
                        continue;
                    }
                    note(callbacks, opaque, "unsupported P-slice intra CAVLC macroblock type");
                    free(states);
                    return 0;
                }
                if (pps->transform_8x8_mode_flag && i_info.pred_kind == AVC_MB_PRED_INTRA_4X4) {
                    event.transform_size_8x8_flag = (int)avc_br_read_bit(br);
                    if (br->error) {
                        free(states);
                        return 0;
                    }
                    if (event.transform_size_8x8_flag) {
                        i_info.pred_kind = AVC_MB_PRED_INTRA_8X8;
                    }
                }
                if (!cavlc_parse_intra_mb_pred(br, mb_addr, i_info, callbacks, opaque)) {
                    free(states);
                    return 0;
                }
                if (i_info.pred_kind == AVC_MB_PRED_INTRA_4X4 || i_info.pred_kind == AVC_MB_PRED_INTRA_8X8) {
                    if (!cavlc_coded_block_pattern(br, 1, &event.coded_block_pattern_luma,
                                                   &event.coded_block_pattern_chroma)) {
                        free(states);
                        return 0;
                    }
                } else {
                    event.coded_block_pattern_luma = i_info.cbp_luma;
                    event.coded_block_pattern_chroma = i_info.cbp_chroma;
                }
            } else {
                if (!cavlc_parse_p_inter_mb_pred(br, mb_addr, slice, p_info.shape,
                                                 left, top, callbacks, opaque,
                                                 &pred_event)) {
                    free(states);
                    return 0;
                }
                pred_for_state = &pred_event;
                if (!cavlc_coded_block_pattern(br, 0, &event.coded_block_pattern_luma,
                                               &event.coded_block_pattern_chroma)) {
                    free(states);
                    return 0;
                }
            }
            if (sps->chroma_format_idc == 0) {
                event.coded_block_pattern_chroma = 0;
            }
            if (pps->transform_8x8_mode_flag && event.coded_block_pattern_luma &&
                p_info.shape != AVC_P_MB_INTRA) {
                event.transform_size_8x8_flag = (int)avc_br_read_bit(br);
            }
            if (event.coded_block_pattern_luma || event.coded_block_pattern_chroma ||
                (p_info.shape == AVC_P_MB_INTRA && i_info.pred_kind == AVC_MB_PRED_INTRA_16X16)) {
                event.mb_qp_delta = avc_br_read_se(br);
            }
            if (callbacks.on_macroblock) {
                callbacks.on_macroblock(opaque, &event);
            }
            summary->macroblocks_seen++;
            if (!cavlc_emit_luma_residuals(br, mb_addr,
                                           p_info.shape == AVC_P_MB_INTRA ? i_info.pred_kind : AVC_MB_PRED_INTER,
                                           event.coded_block_pattern_luma,
                                           event.transform_size_8x8_flag,
                                           left, top, curr,
                                           callbacks, opaque) ||
                !cavlc_emit_chroma_residuals(br, mb_addr, sps,
                                             event.coded_block_pattern_chroma,
                                             left, top, curr,
                                             callbacks, opaque)) {
                free(states);
                return 0;
            }
            save_mb_state(states, max_mbs, &event, pred_for_state);
        } else if (slice->slice_kind == AVC_SLICE_B) {
            avc_b_mb_type_info_t b_info = classify_b_mb_type(event.mb_type);
            avc_i_mb_type_info_t i_info = b_info.intra;

            if (b_info.shape == AVC_B_MB_UNKNOWN) {
                note(callbacks, opaque, "unsupported B-slice CAVLC macroblock type");
                free(states);
                return 0;
            }
            if (b_info.shape == AVC_B_MB_INTRA) {
                if (i_info.is_pcm) {
                    if (!cavlc_consume_ipcm(br, sps, &event)) {
                        free(states);
                        return 0;
                    }
                    cavlc_mark_ipcm_nonzero(curr, sps);
                    if (callbacks.on_macroblock) {
                        callbacks.on_macroblock(opaque, &event);
                    }
                    save_mb_state(states, max_mbs, &event, NULL);
                    summary->macroblocks_seen++;
                    mb_addr++;
                    continue;
                }
                if (i_info.pred_kind == AVC_MB_PRED_UNKNOWN) {
                    note(callbacks, opaque, "unsupported B-slice intra CAVLC macroblock type");
                    free(states);
                    return 0;
                }
                if (pps->transform_8x8_mode_flag && i_info.pred_kind == AVC_MB_PRED_INTRA_4X4) {
                    event.transform_size_8x8_flag = (int)avc_br_read_bit(br);
                    if (br->error) {
                        free(states);
                        return 0;
                    }
                    if (event.transform_size_8x8_flag) {
                        i_info.pred_kind = AVC_MB_PRED_INTRA_8X8;
                    }
                }
                if (!cavlc_parse_intra_mb_pred(br, mb_addr, i_info, callbacks, opaque)) {
                    free(states);
                    return 0;
                }
                if (i_info.pred_kind == AVC_MB_PRED_INTRA_4X4 || i_info.pred_kind == AVC_MB_PRED_INTRA_8X8) {
                    if (!cavlc_coded_block_pattern(br, 1, &event.coded_block_pattern_luma,
                                                   &event.coded_block_pattern_chroma)) {
                        free(states);
                        return 0;
                    }
                } else {
                    event.coded_block_pattern_luma = i_info.cbp_luma;
                    event.coded_block_pattern_chroma = i_info.cbp_chroma;
                }
            } else {
                if (!cavlc_parse_b_inter_mb_pred(br, mb_addr, slice, b_info,
                                                 left, top, callbacks, opaque,
                                                 &pred_event)) {
                    free(states);
                    return 0;
                }
                pred_for_state = &pred_event;
                if (!cavlc_coded_block_pattern(br, 0, &event.coded_block_pattern_luma,
                                               &event.coded_block_pattern_chroma)) {
                    free(states);
                    return 0;
                }
            }
            if (sps->chroma_format_idc == 0) {
                event.coded_block_pattern_chroma = 0;
            }
            if (pps->transform_8x8_mode_flag && event.coded_block_pattern_luma &&
                b_info.shape != AVC_B_MB_INTRA) {
                event.transform_size_8x8_flag = (int)avc_br_read_bit(br);
            }
            if (event.coded_block_pattern_luma || event.coded_block_pattern_chroma ||
                (b_info.shape == AVC_B_MB_INTRA && i_info.pred_kind == AVC_MB_PRED_INTRA_16X16)) {
                event.mb_qp_delta = avc_br_read_se(br);
            }
            if (callbacks.on_macroblock) {
                callbacks.on_macroblock(opaque, &event);
            }
            summary->macroblocks_seen++;
            if (!cavlc_emit_luma_residuals(br, mb_addr,
                                           b_info.shape == AVC_B_MB_INTRA ? i_info.pred_kind : AVC_MB_PRED_INTER,
                                           event.coded_block_pattern_luma,
                                           event.transform_size_8x8_flag,
                                           left, top, curr,
                                           callbacks, opaque) ||
                !cavlc_emit_chroma_residuals(br, mb_addr, sps,
                                             event.coded_block_pattern_chroma,
                                             left, top, curr,
                                             callbacks, opaque)) {
                free(states);
                return 0;
            }
            save_mb_state(states, max_mbs, &event, pred_for_state);
        }
        mb_addr++;
    }

    summary->complete = mb_addr >= max_mbs;
    free(states);
    return !br->error;
}

static int parse_cabac_slice_data(avc_bitreader_t *br,
                                  const avc_slice_header_t *slice,
                                  const avc_pps_t *pps,
                                  const avc_sps_t *sps,
                                  avc_macroblock_callbacks_t callbacks,
                                  void *opaque,
                                  avc_slice_data_summary_t *summary)
{
    avc_cabac_decoder_t cabac;
    size_t byte_pos;
    unsigned slice_qp_y;
    uint32_t mb_addr = slice->first_mb_in_slice;
    uint32_t max_mbs = pic_size_in_mbs(sps);
    uint32_t width = pic_width_in_mbs(sps);
    int prev_mb_qp_delta_nonzero = 0;
    avc_mb_state_t *states = (avc_mb_state_t *)calloc(max_mbs ? max_mbs : 1u, sizeof(*states));

    if (!states) {
        return 0;
    }

    while ((br->bit_pos & 7u) != 0) {
        avc_br_read_bit(br);
    }
    byte_pos = br->bit_pos >> 3;
    if (byte_pos >= br->size) {
        free(states);
        return !br->error;
    }

    slice_qp_y = (unsigned)(26 + pps->pic_init_qp_minus26 + slice->slice_qp_delta);
    avc_cabac_init(&cabac, br->data + byte_pos, br->size - byte_pos);
    if (!avc_cabac_init_contexts(&cabac, slice_qp_y, slice->cabac_init_idc, slice->slice_kind)) {
        free(states);
        return 0;
    }
    summary->cabac_initialized = 1;

    while (mb_addr < max_mbs && !cabac.error) {
        avc_macroblock_event_t event;
        const avc_mb_state_t *left;
        const avc_mb_state_t *top;
        avc_i_mb_type_info_t i_info;
        avc_p_mb_type_info_t p_info;
        avc_b_mb_type_info_t b_info;
        avc_mb_pred_event_t pred_event;
        avc_mb_pred_event_t *pred_for_state = NULL;
        int skipped = 0;

        event = (avc_macroblock_event_t){0};
        event.address = mb_addr;
        event.entropy = AVC_MB_ENTROPY_CABAC;
        i_info = (avc_i_mb_type_info_t){0};
        p_info = (avc_p_mb_type_info_t){0};
        b_info = (avc_b_mb_type_info_t){0};
        pred_event = (avc_mb_pred_event_t){0};
        neighbor_states(states, max_mbs, width, mb_addr, &left, &top);

        if (slice->slice_kind == AVC_SLICE_P || slice->slice_kind == AVC_SLICE_SP ||
            slice->slice_kind == AVC_SLICE_B) {
            skipped = avc_cabac_decode_mb_skip_flag(&cabac, slice->slice_kind,
                                                    left && left->available, left ? left->skipped : 0,
                                                    top && top->available, top ? top->skipped : 0);
            if (cabac.error) {
                free(states);
                return 0;
            }
            if (skipped) {
                avc_mb_pred_event_t skipped_pred;
                avc_mb_pred_event_t *skipped_pred_ptr = NULL;

                event.skipped = 1;
                if (slice->slice_kind == AVC_SLICE_B) {
                    emit_b_direct_pred(mb_addr, slice, callbacks, opaque, &skipped_pred);
                    skipped_pred_ptr = &skipped_pred;
                } else {
                    emit_p_skip_pred(mb_addr, left, top, callbacks, opaque, &skipped_pred);
                    skipped_pred_ptr = &skipped_pred;
                }
                if (callbacks.on_macroblock) {
                    callbacks.on_macroblock(opaque, &event);
                }
                save_mb_state(states, max_mbs, &event, skipped_pred_ptr);
                summary->macroblocks_seen++;
                mb_addr++;
                prev_mb_qp_delta_nonzero = 0;
                if (avc_cabac_decode_terminate(&cabac)) {
                    summary->complete = 1;
                    free(states);
                    return !cabac.error;
                }
                continue;
            }
        }

        if (slice->slice_kind == AVC_SLICE_I || slice->slice_kind == AVC_SLICE_SI) {
            if (!avc_cabac_decode_mb_type_i(&cabac, &event.mb_type)) {
                notef(callbacks, opaque, "CABAC mb_type I decode failed mb=%u bit=%zu",
                      mb_addr, cabac.bit_pos);
                free(states);
                return 0;
            }
            i_info = classify_i_mb_type(event.mb_type);
        } else if (slice->slice_kind == AVC_SLICE_B) {
            if (!avc_cabac_decode_mb_type_b(&cabac, &event.mb_type)) {
                notef(callbacks, opaque, "CABAC mb_type B decode failed mb=%u bit=%zu",
                      mb_addr, cabac.bit_pos);
                free(states);
                return 0;
            }
            b_info = classify_b_mb_type(event.mb_type);
            if (b_info.shape == AVC_B_MB_UNKNOWN) {
                notef(callbacks, opaque,
                      "unsupported B-slice CABAC macroblock type mb=%u mb_type=%u bit=%zu",
                      mb_addr, event.mb_type, cabac.bit_pos);
                free(states);
                return 0;
            }
            if (b_info.shape == AVC_B_MB_INTRA) {
                i_info = b_info.intra;
            }
        } else {
            if (!avc_cabac_decode_mb_type_p(&cabac, &event.mb_type)) {
                notef(callbacks, opaque, "CABAC mb_type P decode failed mb=%u bit=%zu",
                      mb_addr, cabac.bit_pos);
                free(states);
                return 0;
            }
            p_info = classify_p_mb_type(event.mb_type);
            if (p_info.shape == AVC_P_MB_UNKNOWN) {
                notef(callbacks, opaque,
                      "unsupported P-slice CABAC macroblock type mb=%u mb_type=%u bit=%zu",
                      mb_addr, event.mb_type, cabac.bit_pos);
                free(states);
                return 0;
            }
            if (p_info.shape == AVC_P_MB_INTRA) {
                i_info = p_info.intra;
            }
        }

        if (i_info.is_pcm) {
            event.transform_size_8x8_flag = 0;
            if (!cabac_consume_ipcm(&cabac, sps, &event)) {
                free(states);
                return 0;
            }
            if (callbacks.on_macroblock) {
                callbacks.on_macroblock(opaque, &event);
            }
            save_mb_state(states, max_mbs, &event, NULL);
            summary->macroblocks_seen++;
            mb_addr++;
            prev_mb_qp_delta_nonzero = 0;
            if (mb_addr >= max_mbs || avc_cabac_decode_terminate(&cabac)) {
                summary->complete = 1;
                free(states);
                return !cabac.error;
            }
            continue;
        }

        if (i_info.pred_kind == AVC_MB_PRED_INTRA_4X4 ||
            i_info.pred_kind == AVC_MB_PRED_INTRA_8X8 ||
            i_info.pred_kind == AVC_MB_PRED_INTRA_16X16) {
            if (pps->transform_8x8_mode_flag && i_info.pred_kind == AVC_MB_PRED_INTRA_4X4) {
                event.transform_size_8x8_flag =
                    avc_cabac_decode_transform_size_8x8_flag(&cabac,
                                                             left ? left->transform_size_8x8_flag : 0,
                                                             top ? top->transform_size_8x8_flag : 0);
                if (cabac.error) {
                    notef(callbacks, opaque,
                          "CABAC transform_size_8x8_flag decode failed mb=%u bit=%zu",
                          mb_addr, cabac.bit_pos);
                    free(states);
                    return 0;
                }
                if (event.transform_size_8x8_flag) {
                    i_info.pred_kind = AVC_MB_PRED_INTRA_8X8;
                }
            }
            if (!cabac_parse_intra_mb_pred(&cabac, mb_addr, i_info, callbacks, opaque)) {
                notef(callbacks, opaque, "CABAC intra mb_pred failed mb=%u mb_type=%u bit=%zu",
                      mb_addr, event.mb_type, cabac.bit_pos);
                free(states);
                return 0;
            }
            if (i_info.pred_kind == AVC_MB_PRED_INTRA_4X4 || i_info.pred_kind == AVC_MB_PRED_INTRA_8X8) {
                if (!avc_cabac_decode_coded_block_pattern_luma(&cabac,
                                                               left && left->available,
                                                               left ? left->coded_block_pattern_luma : 0,
                                                               top && top->available,
                                                               top ? top->coded_block_pattern_luma : 0,
                                                               &event.coded_block_pattern_luma)) {
                    notef(callbacks, opaque,
                          "CABAC coded_block_pattern_luma decode failed mb=%u mb_type=%u bit=%zu",
                          mb_addr, event.mb_type, cabac.bit_pos);
                    free(states);
                    return 0;
                }
                if (!avc_cabac_decode_coded_block_pattern_chroma(&cabac,
                                                                 left && left->available,
                                                                 left ? left->coded_block_pattern_chroma : 0,
                                                                 top && top->available,
                                                                 top ? top->coded_block_pattern_chroma : 0,
                                                                 &event.coded_block_pattern_chroma)) {
                    notef(callbacks, opaque,
                          "CABAC coded_block_pattern_chroma decode failed mb=%u mb_type=%u bit=%zu",
                          mb_addr, event.mb_type, cabac.bit_pos);
                    free(states);
                    return 0;
                }
            } else {
                event.coded_block_pattern_luma = i_info.cbp_luma;
                event.coded_block_pattern_chroma = i_info.cbp_chroma;
            }
            if (event.coded_block_pattern_luma || event.coded_block_pattern_chroma ||
                i_info.pred_kind == AVC_MB_PRED_INTRA_16X16) {
                if (!avc_cabac_decode_mb_qp_delta(&cabac, prev_mb_qp_delta_nonzero,
                                                  &event.mb_qp_delta)) {
                    notef(callbacks, opaque,
                          "CABAC mb_qp_delta decode failed mb=%u mb_type=%u bit=%zu",
                          mb_addr, event.mb_type, cabac.bit_pos);
                    free(states);
                    return 0;
                }
            }
        } else if (p_info.shape == AVC_P_MB_L0_16X16 ||
                   p_info.shape == AVC_P_MB_L0_L0_16X8 ||
                   p_info.shape == AVC_P_MB_L0_L0_8X16 ||
                   p_info.shape == AVC_P_MB_8X8 ||
                   p_info.shape == AVC_P_MB_8X8REF0) {
            if (!cabac_parse_p_inter_mb_pred(&cabac, mb_addr, slice, p_info.shape,
                                             left, top, callbacks, opaque,
                                             &pred_event)) {
                notef(callbacks, opaque, "CABAC P inter mb_pred failed mb=%u mb_type=%u bit=%zu",
                      mb_addr, event.mb_type, cabac.bit_pos);
                free(states);
                return 0;
            }
            pred_for_state = &pred_event;
            if (!avc_cabac_decode_coded_block_pattern_luma(&cabac,
                                                           left && left->available,
                                                           left ? left->coded_block_pattern_luma : 0,
                                                           top && top->available,
                                                           top ? top->coded_block_pattern_luma : 0,
                                                           &event.coded_block_pattern_luma)) {
                notef(callbacks, opaque,
                      "CABAC coded_block_pattern_luma decode failed mb=%u mb_type=%u bit=%zu",
                      mb_addr, event.mb_type, cabac.bit_pos);
                free(states);
                return 0;
            }
            if (!avc_cabac_decode_coded_block_pattern_chroma(&cabac,
                                                             left && left->available,
                                                             left ? left->coded_block_pattern_chroma : 0,
                                                             top && top->available,
                                                             top ? top->coded_block_pattern_chroma : 0,
                                                             &event.coded_block_pattern_chroma)) {
                notef(callbacks, opaque,
                      "CABAC coded_block_pattern_chroma decode failed mb=%u mb_type=%u bit=%zu",
                      mb_addr, event.mb_type, cabac.bit_pos);
                free(states);
                return 0;
            }
            if (pps->transform_8x8_mode_flag && event.coded_block_pattern_luma) {
                event.transform_size_8x8_flag =
                    avc_cabac_decode_transform_size_8x8_flag(&cabac,
                                                             left ? left->transform_size_8x8_flag : 0,
                                                             top ? top->transform_size_8x8_flag : 0);
                if (cabac.error) {
                    notef(callbacks, opaque,
                          "CABAC transform_size_8x8_flag decode failed mb=%u bit=%zu",
                          mb_addr, cabac.bit_pos);
                    free(states);
                    return 0;
                }
            }
            if (event.coded_block_pattern_luma || event.coded_block_pattern_chroma) {
                if (!avc_cabac_decode_mb_qp_delta(&cabac, prev_mb_qp_delta_nonzero,
                                                  &event.mb_qp_delta)) {
                    notef(callbacks, opaque,
                          "CABAC mb_qp_delta decode failed mb=%u mb_type=%u bit=%zu",
                          mb_addr, event.mb_type, cabac.bit_pos);
                    free(states);
                    return 0;
                }
            }
        } else if (b_info.shape == AVC_B_MB_DIRECT ||
                   b_info.shape == AVC_B_MB_INTER ||
                   b_info.shape == AVC_B_MB_8X8) {
            if (!cabac_parse_b_inter_mb_pred(&cabac, mb_addr, slice, b_info,
                                             left, top, callbacks, opaque,
                                             &pred_event)) {
                notef(callbacks, opaque, "CABAC B inter mb_pred failed mb=%u mb_type=%u bit=%zu",
                      mb_addr, event.mb_type, cabac.bit_pos);
                free(states);
                return 0;
            }
            pred_for_state = &pred_event;
            if (!avc_cabac_decode_coded_block_pattern_luma(&cabac,
                                                           left && left->available,
                                                           left ? left->coded_block_pattern_luma : 0,
                                                           top && top->available,
                                                           top ? top->coded_block_pattern_luma : 0,
                                                           &event.coded_block_pattern_luma)) {
                notef(callbacks, opaque,
                      "CABAC coded_block_pattern_luma decode failed mb=%u mb_type=%u bit=%zu",
                      mb_addr, event.mb_type, cabac.bit_pos);
                free(states);
                return 0;
            }
            if (!avc_cabac_decode_coded_block_pattern_chroma(&cabac,
                                                             left && left->available,
                                                             left ? left->coded_block_pattern_chroma : 0,
                                                             top && top->available,
                                                             top ? top->coded_block_pattern_chroma : 0,
                                                             &event.coded_block_pattern_chroma)) {
                notef(callbacks, opaque,
                      "CABAC coded_block_pattern_chroma decode failed mb=%u mb_type=%u bit=%zu",
                      mb_addr, event.mb_type, cabac.bit_pos);
                free(states);
                return 0;
            }
            if (pps->transform_8x8_mode_flag && event.coded_block_pattern_luma) {
                event.transform_size_8x8_flag =
                    avc_cabac_decode_transform_size_8x8_flag(&cabac,
                                                             left ? left->transform_size_8x8_flag : 0,
                                                             top ? top->transform_size_8x8_flag : 0);
                if (cabac.error) {
                    notef(callbacks, opaque,
                          "CABAC transform_size_8x8_flag decode failed mb=%u bit=%zu",
                          mb_addr, cabac.bit_pos);
                    free(states);
                    return 0;
                }
            }
            if (event.coded_block_pattern_luma || event.coded_block_pattern_chroma) {
                if (!avc_cabac_decode_mb_qp_delta(&cabac, prev_mb_qp_delta_nonzero,
                                                  &event.mb_qp_delta)) {
                    notef(callbacks, opaque,
                          "CABAC mb_qp_delta decode failed mb=%u mb_type=%u bit=%zu",
                          mb_addr, event.mb_type, cabac.bit_pos);
                    free(states);
                    return 0;
                }
            }
        }

        if (callbacks.on_macroblock) {
            callbacks.on_macroblock(opaque, &event);
        }
        save_mb_state(states, max_mbs, &event, pred_for_state);
        summary->macroblocks_seen++;

        if (i_info.pred_kind == AVC_MB_PRED_INTRA_4X4 ||
            i_info.pred_kind == AVC_MB_PRED_INTRA_8X8 ||
            i_info.pred_kind == AVC_MB_PRED_INTRA_16X16) {
            avc_mb_state_t *curr = state_for(states, max_mbs, mb_addr);
            if (!cabac_emit_luma_residuals(&cabac, mb_addr, i_info.pred_kind,
                                           event.coded_block_pattern_luma,
                                           event.transform_size_8x8_flag,
                                           left, top, curr,
                                           callbacks, opaque)) {
                free(states);
                return 0;
            }
            if (!cabac_emit_chroma_residuals(&cabac, mb_addr, sps,
                                             event.coded_block_pattern_chroma,
                                             left, top, curr,
                                             callbacks, opaque)) {
                free(states);
                return 0;
            }
        } else if (p_info.shape == AVC_P_MB_L0_16X16 ||
                   p_info.shape == AVC_P_MB_L0_L0_16X8 ||
                   p_info.shape == AVC_P_MB_L0_L0_8X16 ||
                   p_info.shape == AVC_P_MB_8X8 ||
                   p_info.shape == AVC_P_MB_8X8REF0) {
            avc_mb_state_t *curr = state_for(states, max_mbs, mb_addr);
            if (!cabac_emit_luma_residuals(&cabac, mb_addr, AVC_MB_PRED_INTER,
                                           event.coded_block_pattern_luma,
                                           event.transform_size_8x8_flag,
                                           left, top, curr,
                                           callbacks, opaque)) {
                free(states);
                return 0;
            }
            if (!cabac_emit_chroma_residuals(&cabac, mb_addr, sps,
                                             event.coded_block_pattern_chroma,
                                             left, top, curr,
                                             callbacks, opaque)) {
                free(states);
                return 0;
            }
        } else if (b_info.shape == AVC_B_MB_DIRECT ||
                   b_info.shape == AVC_B_MB_INTER ||
                   b_info.shape == AVC_B_MB_8X8) {
            avc_mb_state_t *curr = state_for(states, max_mbs, mb_addr);
            if (!cabac_emit_luma_residuals(&cabac, mb_addr, AVC_MB_PRED_INTER,
                                           event.coded_block_pattern_luma,
                                           event.transform_size_8x8_flag,
                                           left, top, curr,
                                           callbacks, opaque)) {
                free(states);
                return 0;
            }
            if (!cabac_emit_chroma_residuals(&cabac, mb_addr, sps,
                                             event.coded_block_pattern_chroma,
                                             left, top, curr,
                                             callbacks, opaque)) {
                free(states);
                return 0;
            }
        } else if (i_info.pred_kind == AVC_MB_PRED_UNKNOWN) {
            note(callbacks, opaque, "unsupported CABAC macroblock type");
            cabac.error = 1;
            free(states);
            return 0;
        }

        mb_addr++;
        prev_mb_qp_delta_nonzero = event.mb_qp_delta != 0;
        if (mb_addr >= max_mbs || avc_cabac_decode_terminate(&cabac)) {
            summary->complete = 1;
            free(states);
            return !cabac.error;
        }
    }

    summary->complete = mb_addr >= max_mbs;
    free(states);
    return !cabac.error;
}

int avc_parse_slice_data(const uint8_t *rbsp, size_t rbsp_size,
                         const avc_slice_header_t *slice,
                         const avc_parameter_sets_t *sets,
                         avc_macroblock_callbacks_t callbacks,
                         void *opaque,
                         avc_slice_data_summary_t *summary)
{
    avc_bitreader_t br;
    const avc_pps_t *pps;
    const avc_sps_t *sps;

    *summary = (avc_slice_data_summary_t){0};
    if (!slice->valid || slice->pic_parameter_set_id >= AVC_MAX_PPS) {
        return 0;
    }
    pps = &sets->pps[slice->pic_parameter_set_id];
    if (!pps->present || pps->seq_parameter_set_id >= AVC_MAX_SPS) {
        return 0;
    }
    sps = &sets->sps[pps->seq_parameter_set_id];
    if (!sps->present) {
        return 0;
    }

    summary->entropy_coding_mode_flag = pps->entropy_coding_mode_flag;
    summary->max_macroblocks = pic_size_in_mbs(sps);

    avc_br_init(&br, rbsp, rbsp_size);
    if (slice->header_bits > rbsp_size * 8) {
        return 0;
    }
    br.bit_pos = slice->header_bits;

    if (pps->entropy_coding_mode_flag) {
        return parse_cabac_slice_data(&br, slice, pps, sps, callbacks, opaque, summary);
    }
    return parse_cavlc_slice_data(&br, slice, pps, sps, callbacks, opaque, summary);
}
