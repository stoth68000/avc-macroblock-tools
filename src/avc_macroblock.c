#include "avc/avc_macroblock.h"
#include "avc/avc_bitreader.h"
#include "avc/avc_cabac.h"
#include "avc_macroblock_layout.h"
#include "avc_macroblock_types.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int available;
    int skipped;
    int mb_field_decoding_flag;
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
    avc_residual_kind_t kind;
    unsigned max_coeff;
    unsigned ctx_block_cat;
    unsigned coded_ctx_base;
    unsigned sig_ctx_base;
    unsigned last_ctx_base;
    unsigned level_ctx_base;
} avc_cabac_residual_plan_t;

typedef struct {
    uint8_t available[4][4];
    int16_t mvd[4][4][2];
} avc_sub_mvd_grid_t;

static const avc_cabac_residual_plan_t residual_plans[] = {
    {AVC_RESIDUAL_LUMA_4X4, 16, 2, 93, 134, 195, 247},
    {AVC_RESIDUAL_LUMA_8X8, 64, 5, 1012, 402, 417, 426},
    {AVC_RESIDUAL_LUMA_16X16_DC, 16, 0, 85, 105, 166, 227},
    {AVC_RESIDUAL_LUMA_16X16_AC, 15, 1, 89, 120, 181, 237},
    {AVC_RESIDUAL_CHROMA_DC, 4, 3, 97, 149, 210, 257},
    {AVC_RESIDUAL_CHROMA_AC, 15, 4, 101, 152, 213, 266}
};

static void neighbor_states4(avc_mb_state_t *states, uint32_t count, uint32_t width,
                             uint32_t address,
                             const avc_mb_state_t **left,
                             const avc_mb_state_t **top,
                             const avc_mb_state_t **top_right,
                             const avc_mb_state_t **top_left);

static void note(avc_macroblock_callbacks_t callbacks, void *opaque, const char *message)
{
    if (callbacks.on_note) {
        callbacks.on_note(opaque, message);
    }
}

static void notef(avc_macroblock_callbacks_t callbacks, void *opaque,
                  const char *fmt, ...)
{
    char message[320];
    va_list ap;

    if (!callbacks.on_note) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    callbacks.on_note(opaque, message);
}

static const char *cabac_residual_error_name(int error_syntax)
{
    switch ((avc_cabac_residual_error_t)error_syntax) {
    case AVC_CABAC_RESIDUAL_ERROR_CODED_BLOCK_FLAG:
        return "coded_block_flag";
    case AVC_CABAC_RESIDUAL_ERROR_SIGNIFICANT_COEFF_FLAG:
        return "significant_coeff_flag";
    case AVC_CABAC_RESIDUAL_ERROR_LAST_SIGNIFICANT_COEFF_FLAG:
        return "last_significant_coeff_flag";
    case AVC_CABAC_RESIDUAL_ERROR_COEFF_ABS_LEVEL_MINUS1:
        return "coeff_abs_level_minus1";
    case AVC_CABAC_RESIDUAL_ERROR_COEFF_SIGN_FLAG:
        return "coeff_sign_flag";
    case AVC_CABAC_RESIDUAL_ERROR_COEFF_SCAN_EXHAUSTED:
        return "coeff_scan_exhausted";
    case AVC_CABAC_RESIDUAL_ERROR_INVALID_MAX_COEFF:
        return "invalid_max_coeff";
    case AVC_CABAC_RESIDUAL_ERROR_NONE:
    default:
        return "unknown";
    }
}

static void debug_mb(avc_macroblock_callbacks_t callbacks, void *opaque,
                     uint32_t mb_addr,
                     const char *fmt, ...)
{
    const char *debug_mb_env = getenv("AVC_DEBUG_MB");
    char message[256];
    va_list ap;
    char *end = NULL;
    unsigned long debug_mb_addr;

    if (!debug_mb_env || !*debug_mb_env || !callbacks.on_note) {
        return;
    }
    debug_mb_addr = strtoul(debug_mb_env, &end, 0);
    if (end == debug_mb_env || *end != '\0' || debug_mb_addr != mb_addr) {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    callbacks.on_note(opaque, message);
}

static int trace_mb_enabled(avc_macroblock_callbacks_t callbacks, uint32_t mb_addr)
{
    return callbacks.on_note && callbacks.trace_enabled &&
           mb_addr >= callbacks.trace_first_mb &&
           mb_addr <= callbacks.trace_last_mb;
}

static void trace_mb(avc_macroblock_callbacks_t callbacks, void *opaque,
                     uint32_t mb_addr,
                     const char *fmt, ...)
{
    char message[512];
    va_list ap;

    if (!trace_mb_enabled(callbacks, mb_addr)) {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    callbacks.on_note(opaque, message);
}

static const char *p_mb_shape_name(avc_p_mb_shape_t shape)
{
    switch (shape) {
    case AVC_P_MB_L0_16X16:
        return "P_L0_16x16";
    case AVC_P_MB_L0_L0_16X8:
        return "P_L0_L0_16x8";
    case AVC_P_MB_L0_L0_8X16:
        return "P_L0_L0_8x16";
    case AVC_P_MB_8X8:
        return "P_8x8";
    case AVC_P_MB_8X8REF0:
        return "P_8x8ref0";
    case AVC_P_MB_INTRA:
        return "P_Intra";
    case AVC_P_MB_UNKNOWN:
    default:
        return "unknown";
    }
}

static avc_cavlc_scan_t residual_scan_mode(const avc_slice_header_t *slice,
                                           const avc_sps_t *sps,
                                           const avc_macroblock_event_t *event)
{
    int field_scan = slice->field_pic_flag || event->mb_field_decoding_flag;
    int32_t qpprime_y = event->qp_y + 6 * (int32_t)sps->bit_depth_luma_minus8;
    int transform_bypass = sps->qpprime_y_zero_transform_bypass_flag && qpprime_y == 0;

    if (transform_bypass) {
        return field_scan ? AVC_CAVLC_SCAN_TRANSFORM_BYPASS_FIELD :
                            AVC_CAVLC_SCAN_TRANSFORM_BYPASS_FRAME;
    }
    return field_scan ? AVC_CAVLC_SCAN_FIELD : AVC_CAVLC_SCAN_FRAME;
}

static unsigned residual_component(avc_residual_kind_t kind,
                                   unsigned block_index,
                                   unsigned chroma_format)
{
    unsigned ac_blocks;

    if (kind != AVC_RESIDUAL_CHROMA_DC && kind != AVC_RESIDUAL_CHROMA_AC) {
        return 0;
    }
    if (kind == AVC_RESIDUAL_CHROMA_DC) {
        return block_index < 2 ? block_index + 1u : 0;
    }
    ac_blocks = avc_mb_chroma_ac_blocks_per_component(chroma_format);
    if (ac_blocks == 0) {
        return 0;
    }
    return (block_index / ac_blocks) + 1u;
}

static int8_t residual_scaling_list_index(avc_residual_kind_t kind,
                                          unsigned component,
                                          avc_mb_pred_kind_t pred_kind)
{
    int intra = pred_kind == AVC_MB_PRED_INTRA_4X4 ||
                pred_kind == AVC_MB_PRED_INTRA_8X8 ||
                pred_kind == AVC_MB_PRED_INTRA_16X16;

    if (kind == AVC_RESIDUAL_LUMA_8X8) {
        if (component == 0) {
            return intra ? 0 : 1;
        }
        return (int8_t)(intra ? 2 + (component - 1u) * 2u :
                                3 + (component - 1u) * 2u);
    }
    if (component == 0) {
        return intra ? 0 : 3;
    }
    return (int8_t)(intra ? component : component + 3u);
}

static const uint8_t *active_scaling_list4(const avc_pps_t *pps,
                                           const avc_sps_t *sps,
                                           unsigned index,
                                           uint8_t *present,
                                           uint8_t *use_default)
{
    if (present) {
        *present = 0;
    }
    if (use_default) {
        *use_default = 0;
    }
    if (index >= AVC_SPS_SCALING_LIST_4X4_COUNT) {
        return NULL;
    }
    if (pps && pps->pic_scaling_matrix_present_flag) {
        if (present) {
            *present = pps->pic_scaling_list_present_flag[index];
        }
        if (use_default) {
            *use_default = pps->use_default_scaling_matrix_flag[index];
        }
        return pps->scaling_list_4x4[index];
    }
    if (sps && sps->seq_scaling_matrix_present_flag) {
        if (present) {
            *present = sps->seq_scaling_list_present_flag[index];
        }
        if (use_default) {
            *use_default = sps->use_default_scaling_matrix_flag[index];
        }
        return sps->scaling_list_4x4[index];
    }
    return sps ? sps->scaling_list_4x4[index] : NULL;
}

static const uint8_t *active_scaling_list8(const avc_pps_t *pps,
                                           const avc_sps_t *sps,
                                           unsigned index,
                                           uint8_t *present,
                                           uint8_t *use_default)
{
    if (present) {
        *present = 0;
    }
    if (use_default) {
        *use_default = 0;
    }
    if (index >= AVC_SPS_SCALING_LIST_8X8_COUNT) {
        return NULL;
    }
    if (pps && pps->pic_scaling_matrix_present_flag) {
        if (present) {
            *present = pps->pic_scaling_list_present_flag[index + 6u];
        }
        if (use_default) {
            *use_default = pps->use_default_scaling_matrix_flag[index + 6u];
        }
        return pps->scaling_list_8x8[index];
    }
    if (sps && sps->seq_scaling_matrix_present_flag) {
        if (present) {
            *present = sps->seq_scaling_list_present_flag[index + 6u];
        }
        if (use_default) {
            *use_default = sps->use_default_scaling_matrix_flag[index + 6u];
        }
        return sps->scaling_list_8x8[index];
    }
    return sps ? sps->scaling_list_8x8[index] : NULL;
}

static void fill_residual_metadata(avc_residual_event_t *residual,
                                   const avc_pps_t *pps,
                                   const avc_sps_t *sps,
                                   const avc_macroblock_event_t *event,
                                   avc_mb_pred_kind_t pred_kind)
{
    unsigned component;
    unsigned chroma_format = avc_mb_chroma_array_type(sps);
    int8_t scaling_index;
    uint8_t present = 0;
    uint8_t use_default = 0;

    component = residual_component(residual->block_kind, residual->block_index,
                                   chroma_format);
    residual->component = (uint8_t)component;
    residual->chroma_format_idc = (uint8_t)sps->chroma_format_idc;
    residual->chroma_array_type = (uint8_t)chroma_format;
    residual->separate_colour_plane_flag = sps->separate_colour_plane_flag;
    residual->bit_depth_luma = (uint8_t)(sps->bit_depth_luma_minus8 + 8u);
    residual->bit_depth_chroma = (uint8_t)(sps->bit_depth_chroma_minus8 + 8u);
    residual->qp_y = event->qp_y;
    residual->qp_cb = event->qp_cb;
    residual->qp_cr = event->qp_cr;
    residual->qp_for_block = component == 1 ? event->qp_cb :
                             component == 2 ? event->qp_cr : event->qp_y;
    residual->transform_bypass =
        sps->qpprime_y_zero_transform_bypass_flag &&
        event->qp_y + 6 * (int32_t)sps->bit_depth_luma_minus8 == 0;

    scaling_index = residual_scaling_list_index(residual->block_kind, component,
                                                pred_kind);
    residual->scaling_list_index = scaling_index;
    residual->scaling_list_size = residual->block_kind == AVC_RESIDUAL_LUMA_8X8 ? 8 : 4;
    if (scaling_index < 0) {
        return;
    }
    if (residual->scaling_list_size == 8) {
        (void)active_scaling_list8(pps, sps, (unsigned)scaling_index,
                                   &present, &use_default);
    } else {
        (void)active_scaling_list4(pps, sps, (unsigned)scaling_index,
                                   &present, &use_default);
    }
    residual->scaling_list_present_flag = present;
    residual->scaling_list_use_default_flag = use_default;
}

static int mbaff_frame_flag(const avc_slice_header_t *slice, const avc_sps_t *sps)
{
    return sps->mb_adaptive_frame_field_flag && !slice->field_pic_flag;
}

static int mb_field_flag_present(const avc_slice_header_t *slice,
                                 const avc_sps_t *sps,
                                 uint32_t mb_addr,
                                 int prev_mb_skipped)
{
    return mbaff_frame_flag(slice, sps) && (((mb_addr & 1u) == 0) || prev_mb_skipped);
}

static int cabac_has_substantial_bits_left(const avc_cabac_decoder_t *cabac);

static int mb_pair_field_flag(const avc_mb_state_t *states, uint32_t count, uint32_t mb_addr)
{
    if ((mb_addr & 1u) == 0) {
        return 0;
    }
    if (mb_addr - 1u >= count) {
        return 0;
    }
    return states[mb_addr - 1u].available ? states[mb_addr - 1u].mb_field_decoding_flag : 0;
}

static avc_mb_state_t *state_for(avc_mb_state_t *states, uint32_t count, uint32_t address)
{
    if (address >= count) {
        return NULL;
    }
    return &states[address];
}

static void neighbor_states4(avc_mb_state_t *states, uint32_t count, uint32_t width,
                             uint32_t address,
                             const avc_mb_state_t **left,
                             const avc_mb_state_t **top,
                             const avc_mb_state_t **top_right,
                             const avc_mb_state_t **top_left)
{
    avc_mb_neighbor_addresses_t neighbors;

    *left = NULL;
    *top = NULL;
    *top_right = NULL;
    *top_left = NULL;
    if (!avc_mb_neighbor_addresses(count, width, address, &neighbors)) {
        return;
    }
    if (neighbors.left_available) {
        *left = state_for(states, count, neighbors.left);
    }
    if (neighbors.top_available) {
        *top = state_for(states, count, neighbors.top);
    }
    if (neighbors.top_right_available) {
        *top_right = state_for(states, count, neighbors.top_right);
    }
    if (neighbors.top_left_available) {
        *top_left = state_for(states, count, neighbors.top_left);
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
    state->mb_field_decoding_flag = event->mb_field_decoding_flag;
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

static int state_is_b_direct(const avc_mb_state_t *state)
{
    if (!state || !state->available) {
        return 1;
    }
    if (state->skipped) {
        return 1;
    }
    return state->has_pred && state->pred.partition_count > 0 &&
           state->pred.direct_flag[0];
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
    return avc_cavlc_derive_nC_from_neighbors(left_available, left_count,
                                              top_available, top_count);
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

static int cavlc_chroma_ac_nC(const avc_mb_state_t *curr,
                              const avc_mb_state_t *left,
                              const avc_mb_state_t *top,
                              unsigned chroma_format,
                              unsigned component,
                              unsigned block)
{
    unsigned width = avc_mb_chroma_ac_width(chroma_format);
    unsigned height = avc_mb_chroma_ac_blocks_per_component(chroma_format) / width;
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
        if (block_index < 16) {
            unsigned group = block_index >> 2;
            uint8_t count = clipped_nonzero_count(total_coeff);
            curr->luma_nonzero[block_index] = count;
            if (group < 4 && count > curr->luma_8x8_nonzero[group]) {
                curr->luma_8x8_nonzero[group] = count;
            }
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
        ac_blocks = avc_mb_chroma_ac_blocks_per_component(chroma_format);
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
                                    avc_mb_pred_kind_t pred_kind,
                                    unsigned block_index,
                                    int transform_size_8x8_flag,
                                    int *left_coded,
                                    int *top_coded)
{
    unsigned x;
    unsigned y;

    *left_coded = pred_kind == AVC_MB_PRED_INTER ? 0 : 1;
    *top_coded = pred_kind == AVC_MB_PRED_INTER ? 0 : 1;

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
                                         avc_mb_pred_kind_t pred_kind,
                                         unsigned component,
                                         int *left_coded,
                                         int *top_coded)
{
    if (component >= 2) {
        return 0;
    }
    *left_coded = left && left->available ? left->chroma_dc_nonzero[component] != 0 :
                                            pred_kind != AVC_MB_PRED_INTER;
    *top_coded = top && top->available ? top->chroma_dc_nonzero[component] != 0 :
                                         pred_kind != AVC_MB_PRED_INTER;
    return 1;
}

static int cabac_chroma_ac_cbf_neighbors(const avc_mb_state_t *curr,
                                         const avc_mb_state_t *left,
                                         const avc_mb_state_t *top,
                                         avc_mb_pred_kind_t pred_kind,
                                         unsigned chroma_format,
                                         unsigned component,
                                         unsigned block,
                                         int *left_coded,
                                         int *top_coded)
{
    unsigned width = avc_mb_chroma_ac_width(chroma_format);
    unsigned height = width ? avc_mb_chroma_ac_blocks_per_component(chroma_format) / width : 0;
    unsigned x;
    unsigned y;

    *left_coded = pred_kind == AVC_MB_PRED_INTER ? 0 : 1;
    *top_coded = pred_kind == AVC_MB_PRED_INTER ? 0 : 1;

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
    unsigned chroma_format = avc_mb_chroma_array_type(sps);
    unsigned ac_blocks = avc_mb_chroma_ac_blocks_per_component(chroma_format);

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

static unsigned pred_ref_idx_l0(const avc_mb_pred_event_t *pred, unsigned partition)
{
    if (!pred || pred->partition_count == 0 || partition >= pred->partition_count) {
        return 0;
    }
    return pred->ref_idx_l0[partition];
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

static const avc_mb_pred_event_t *state_pred_l0(const avc_mb_state_t *state)
{
    if (!state || !state->available || !state->has_pred ||
        state->pred.kind != AVC_MB_PRED_INTER) {
        return NULL;
    }
    return &state->pred;
}

static void sub_mvd_grid_store(avc_sub_mvd_grid_t *grid,
                               unsigned x,
                               unsigned y,
                               unsigned width,
                               unsigned height,
                               const int16_t mvd[2])
{
    unsigned start_x = x / 4u;
    unsigned start_y = y / 4u;
    unsigned end_x = (x + width + 3u) / 4u;
    unsigned end_y = (y + height + 3u) / 4u;
    unsigned gx;
    unsigned gy;

    if (!grid) {
        return;
    }
    if (end_x > 4u) {
        end_x = 4u;
    }
    if (end_y > 4u) {
        end_y = 4u;
    }
    for (gy = start_y; gy < end_y; gy++) {
        for (gx = start_x; gx < end_x; gx++) {
            grid->available[gy][gx] = 1;
            grid->mvd[gy][gx][0] = mvd[0];
            grid->mvd[gy][gx][1] = mvd[1];
        }
    }
}

static unsigned sub_mvd_grid_abs_at(const avc_sub_mvd_grid_t *grid,
                                    unsigned x,
                                    unsigned y,
                                    unsigned component,
                                    int *available)
{
    unsigned gx = x / 4u;
    unsigned gy = y / 4u;

    if (available) {
        *available = 0;
    }
    if (!grid || component > 1 || gx >= 4u || gy >= 4u ||
        !grid->available[gy][gx]) {
        return 0;
    }
    if (available) {
        *available = 1;
    }
    return abs_i16(grid->mvd[gy][gx][component]);
}

static void sub_partition_position(const avc_mb_pred_event_t *pred,
                                   unsigned partition,
                                   unsigned sub,
                                   unsigned *x,
                                   unsigned *y,
                                   unsigned *width,
                                   unsigned *height)
{
    if (!avc_mb_pred_sub_partition_position(pred, partition, sub, x, y, width, height)) {
        *x = 0;
        *y = 0;
        *width = 0;
        *height = 0;
    }
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

typedef struct {
    const avc_mb_pred_event_t *pred;
    unsigned partition;
    int available;
} avc_mv_candidate_t;

static int pred_partition_at(const avc_mb_pred_event_t *pred,
                             unsigned x,
                             unsigned y,
                             unsigned *partition)
{
    return avc_mb_pred_partition_at(pred, x, y, partition);
}

static avc_mv_candidate_t current_candidate_at(const avc_mb_pred_event_t *pred,
                                               int x,
                                               int y)
{
    avc_mv_candidate_t candidate = {0};
    unsigned partition;

    if (x < 0 || x >= 16 || y < 0 || y >= 16) {
        return candidate;
    }
    if (pred_partition_at(pred, (unsigned)x, (unsigned)y, &partition)) {
        candidate.pred = pred;
        candidate.partition = partition;
        candidate.available = 1;
    }
    return candidate;
}

static avc_mv_candidate_t neighbor_candidate_at(const avc_mb_state_t *neighbor,
                                                unsigned x,
                                                unsigned y)
{
    avc_mv_candidate_t candidate = {0};
    unsigned partition;

    if (!neighbor || !neighbor->available || !neighbor->has_pred) {
        return candidate;
    }
    if (pred_partition_at(&neighbor->pred, x, y, &partition)) {
        candidate.pred = &neighbor->pred;
        candidate.partition = partition;
        candidate.available = 1;
    }
    return candidate;
}

static avc_mv_candidate_t mv_candidate_a(const avc_mb_pred_event_t *pred,
                                         unsigned partition,
                                         const avc_mb_state_t *left)
{
    int x = (int)pred->partition_x[partition] - 1;
    int y = (int)pred->partition_y[partition] +
            (int)pred->partition_height[partition] - 1;

    if (x >= 0) {
        return current_candidate_at(pred, x, y);
    }
    if (y >= 0 && y < 16) {
        return neighbor_candidate_at(left, 15, (unsigned)y);
    }
    return (avc_mv_candidate_t){0};
}

static avc_mv_candidate_t mv_candidate_b(const avc_mb_pred_event_t *pred,
                                         unsigned partition,
                                         const avc_mb_state_t *top)
{
    int x = (int)pred->partition_x[partition];
    int y = (int)pred->partition_y[partition] - 1;

    if (y >= 0) {
        return current_candidate_at(pred, x, y);
    }
    if (x >= 0 && x < 16) {
        return neighbor_candidate_at(top, (unsigned)x, 15);
    }
    return (avc_mv_candidate_t){0};
}

static avc_mv_candidate_t mv_candidate_c(const avc_mb_pred_event_t *pred,
                                         unsigned partition,
                                         const avc_mb_state_t *top,
                                         const avc_mb_state_t *top_right)
{
    int x = (int)pred->partition_x[partition] +
            (int)pred->partition_width[partition];
    int y = (int)pred->partition_y[partition] - 1;

    if (y >= 0) {
        if (x >= 0 && x < 16) {
            return current_candidate_at(pred, x, y);
        }
        return (avc_mv_candidate_t){0};
    }
    if (x >= 0 && x < 16) {
        return neighbor_candidate_at(top, (unsigned)x, 15);
    }
    if (x >= 16 && x < 32) {
        return neighbor_candidate_at(top_right, (unsigned)(x - 16), 15);
    }
    return (avc_mv_candidate_t){0};
}

static avc_mv_candidate_t mv_candidate_d(const avc_mb_pred_event_t *pred,
                                         unsigned partition,
                                         const avc_mb_state_t *left,
                                         const avc_mb_state_t *top,
                                         const avc_mb_state_t *top_left)
{
    int x = (int)pred->partition_x[partition] - 1;
    int y = (int)pred->partition_y[partition] - 1;

    if (x >= 0 && y >= 0) {
        return current_candidate_at(pred, x, y);
    }
    if (x < 0 && y >= 0) {
        return neighbor_candidate_at(left, 15, (unsigned)y);
    }
    if (x >= 0 && y < 0) {
        return neighbor_candidate_at(top, (unsigned)x, 15);
    }
    return neighbor_candidate_at(top_left, 15, 15);
}

static unsigned candidate_ref_idx_l0(avc_mv_candidate_t candidate)
{
    if (!candidate.available || (candidate.pred->list_mask[candidate.partition] & 1u) == 0) {
        return 0;
    }
    return candidate.pred->ref_idx_l0[candidate.partition];
}

static unsigned candidate_ref_idx_l1(avc_mv_candidate_t candidate)
{
    if (!candidate.available || (candidate.pred->list_mask[candidate.partition] & 2u) == 0) {
        return 0;
    }
    return candidate.pred->ref_idx_l1[candidate.partition];
}

static unsigned candidate_abs_mvd_l0(avc_mv_candidate_t candidate, unsigned component)
{
    if (!candidate.available || component > 1 ||
        (candidate.pred->list_mask[candidate.partition] & 1u) == 0) {
        return 0;
    }
    return abs_i16(candidate.pred->mvd_l0[candidate.partition][component]);
}

static unsigned candidate_abs_mvd_l1(avc_mv_candidate_t candidate, unsigned component)
{
    if (!candidate.available || component > 1 ||
        (candidate.pred->list_mask[candidate.partition] & 2u) == 0) {
        return 0;
    }
    return abs_i16(candidate.pred->mvd_l1[candidate.partition][component]);
}

static int candidate_mv_l0(avc_mv_candidate_t candidate, unsigned ref_idx, int16_t mv[2])
{
    if (!candidate.available ||
        (candidate.pred->list_mask[candidate.partition] & 1u) == 0 ||
        candidate.pred->ref_idx_l0[candidate.partition] != ref_idx) {
        mv[0] = 0;
        mv[1] = 0;
        return 0;
    }
    mv[0] = candidate.pred->mv_l0[candidate.partition][0];
    mv[1] = candidate.pred->mv_l0[candidate.partition][1];
    return 1;
}

static int candidate_mv_l1(avc_mv_candidate_t candidate, unsigned ref_idx, int16_t mv[2])
{
    if (!candidate.available ||
        (candidate.pred->list_mask[candidate.partition] & 2u) == 0 ||
        candidate.pred->ref_idx_l1[candidate.partition] != ref_idx) {
        mv[0] = 0;
        mv[1] = 0;
        return 0;
    }
    mv[0] = candidate.pred->mv_l1[candidate.partition][0];
    mv[1] = candidate.pred->mv_l1[candidate.partition][1];
    return 1;
}

static void geometry_candidates(const avc_mb_pred_event_t *pred,
                                unsigned partition,
                                const avc_mb_state_t *left,
                                const avc_mb_state_t *top,
                                const avc_mb_state_t *top_right,
                                const avc_mb_state_t *top_left,
                                avc_mv_candidate_t *a,
                                avc_mv_candidate_t *b,
                                avc_mv_candidate_t *c)
{
    avc_mv_candidate_t candidate_c;

    *a = mv_candidate_a(pred, partition, left);
    *b = mv_candidate_b(pred, partition, top);
    candidate_c = mv_candidate_c(pred, partition, top, top_right);
    if (!candidate_c.available) {
        candidate_c = mv_candidate_d(pred, partition, left, top, top_left);
    }
    *c = candidate_c;
}

static avc_direct_spatial_candidate_t direct_candidate(avc_mv_candidate_t candidate)
{
    if (!candidate.available) {
        return (avc_direct_spatial_candidate_t){0};
    }
    return (avc_direct_spatial_candidate_t){
        1,
        candidate.pred->list_mask[candidate.partition],
        candidate.pred->ref_idx_l0[candidate.partition],
        candidate.pred->ref_idx_l1[candidate.partition]
    };
}

static void derive_partition_mv_l0(avc_mb_pred_event_t *pred,
                                   unsigned partition,
                                   avc_p_mb_shape_t shape,
                                   const avc_mb_state_t *left,
                                   const avc_mb_state_t *top,
                                   const avc_mb_state_t *top_right,
                                   const avc_mb_state_t *top_left)
{
    int left_current;
    int top_current;
    int top_right_current;
    unsigned left_partition;
    unsigned top_partition;
    unsigned top_right_partition;
    int16_t a[2] = {0, 0};
    int16_t b[2] = {0, 0};
    int16_t c[2] = {0, 0};
    int have_a;
    int have_b;
    int have_c;
    unsigned ref_idx;
    avc_mv_predictor_candidate_t cand_a;
    avc_mv_predictor_candidate_t cand_b;
    avc_mv_predictor_candidate_t cand_c;

    if (!pred || partition >= pred->partition_count || (pred->list_mask[partition] & 1u) == 0) {
        return;
    }

    ref_idx = pred->ref_idx_l0[partition];
    avc_p_mb_partition_neighbors(shape, partition, &left_current, &left_partition,
                          &top_current, &top_partition,
                          &top_right_current, &top_right_partition);
    have_a = left_current ? pred_mv_l0(pred, left_partition, ref_idx, a) :
                            neighbor_mv_l0(left, left_partition, ref_idx, a);
    have_b = top_current ? pred_mv_l0(pred, top_partition, ref_idx, b) :
                           neighbor_mv_l0(top, top_partition, ref_idx, b);
    have_c = top_right_current ? pred_mv_l0(pred, top_right_partition, ref_idx, c) :
                                 neighbor_mv_l0(top_right, top_right_partition, ref_idx, c);
    if (!have_c) {
        have_c = neighbor_mv_l0(top_left, top_partition, ref_idx, c);
    }

    cand_a = (avc_mv_predictor_candidate_t){have_a, {a[0], a[1]}};
    cand_b = (avc_mv_predictor_candidate_t){have_b, {b[0], b[1]}};
    cand_c = (avc_mv_predictor_candidate_t){have_c, {c[0], c[1]}};
    avc_mb_predict_mv(partition, pred->partition_width[partition],
                      pred->partition_height[partition],
                      cand_a, cand_b, cand_c, pred->mv_pred_l0[partition]);
    pred->mv_l0[partition][0] = (int16_t)(pred->mv_pred_l0[partition][0] + pred->mvd_l0[partition][0]);
    pred->mv_l0[partition][1] = (int16_t)(pred->mv_pred_l0[partition][1] + pred->mvd_l0[partition][1]);
}

static void derive_partition_mv_l0_geometry(avc_mb_pred_event_t *pred,
                                            unsigned partition,
                                            const avc_mb_state_t *left,
                                            const avc_mb_state_t *top,
                                            const avc_mb_state_t *top_right,
                                            const avc_mb_state_t *top_left)
{
    avc_mv_candidate_t cand_a;
    avc_mv_candidate_t cand_b;
    avc_mv_candidate_t cand_c;
    int16_t a[2] = {0, 0};
    int16_t b[2] = {0, 0};
    int16_t c[2] = {0, 0};
    int have_a;
    int have_b;
    int have_c;
    unsigned ref_idx;
    unsigned width;
    unsigned height;
    avc_mv_predictor_candidate_t pred_a;
    avc_mv_predictor_candidate_t pred_b;
    avc_mv_predictor_candidate_t pred_c;

    if (!pred || partition >= pred->partition_count ||
        (pred->list_mask[partition] & 1u) == 0) {
        return;
    }
    ref_idx = pred->ref_idx_l0[partition];
    width = pred->partition_width[partition];
    height = pred->partition_height[partition];
    geometry_candidates(pred, partition, left, top, top_right, top_left,
                        &cand_a, &cand_b, &cand_c);
    have_a = candidate_mv_l0(cand_a, ref_idx, a);
    have_b = candidate_mv_l0(cand_b, ref_idx, b);
    have_c = candidate_mv_l0(cand_c, ref_idx, c);

    pred_a = (avc_mv_predictor_candidate_t){have_a, {a[0], a[1]}};
    pred_b = (avc_mv_predictor_candidate_t){have_b, {b[0], b[1]}};
    pred_c = (avc_mv_predictor_candidate_t){have_c, {c[0], c[1]}};
    avc_mb_predict_mv(partition, width, height, pred_a, pred_b, pred_c,
                      pred->mv_pred_l0[partition]);
    pred->mv_l0[partition][0] =
        (int16_t)(pred->mv_pred_l0[partition][0] + pred->mvd_l0[partition][0]);
    pred->mv_l0[partition][1] =
        (int16_t)(pred->mv_pred_l0[partition][1] + pred->mvd_l0[partition][1]);
}

static void derive_partition_mv_l1_geometry(avc_mb_pred_event_t *pred,
                                            unsigned partition,
                                            const avc_mb_state_t *left,
                                            const avc_mb_state_t *top,
                                            const avc_mb_state_t *top_right,
                                            const avc_mb_state_t *top_left)
{
    avc_mv_candidate_t cand_a;
    avc_mv_candidate_t cand_b;
    avc_mv_candidate_t cand_c;
    int16_t a[2] = {0, 0};
    int16_t b[2] = {0, 0};
    int16_t c[2] = {0, 0};
    int have_a;
    int have_b;
    int have_c;
    unsigned ref_idx;
    unsigned width;
    unsigned height;
    avc_mv_predictor_candidate_t pred_a;
    avc_mv_predictor_candidate_t pred_b;
    avc_mv_predictor_candidate_t pred_c;

    if (!pred || partition >= pred->partition_count ||
        (pred->list_mask[partition] & 2u) == 0) {
        return;
    }
    ref_idx = pred->ref_idx_l1[partition];
    width = pred->partition_width[partition];
    height = pred->partition_height[partition];
    geometry_candidates(pred, partition, left, top, top_right, top_left,
                        &cand_a, &cand_b, &cand_c);
    have_a = candidate_mv_l1(cand_a, ref_idx, a);
    have_b = candidate_mv_l1(cand_b, ref_idx, b);
    have_c = candidate_mv_l1(cand_c, ref_idx, c);

    pred_a = (avc_mv_predictor_candidate_t){have_a, {a[0], a[1]}};
    pred_b = (avc_mv_predictor_candidate_t){have_b, {b[0], b[1]}};
    pred_c = (avc_mv_predictor_candidate_t){have_c, {c[0], c[1]}};
    avc_mb_predict_mv(partition, width, height, pred_a, pred_b, pred_c,
                      pred->mv_pred_l1[partition]);
    pred->mv_l1[partition][0] =
        (int16_t)(pred->mv_pred_l1[partition][0] + pred->mvd_l1[partition][0]);
    pred->mv_l1[partition][1] =
        (int16_t)(pred->mv_pred_l1[partition][1] + pred->mvd_l1[partition][1]);
}

static unsigned direct_spatial_ref_idx_geometry(const avc_mb_pred_event_t *pred,
                                                unsigned partition,
                                                const avc_mb_state_t *left,
                                                const avc_mb_state_t *top,
                                                const avc_mb_state_t *top_right,
                                                const avc_mb_state_t *top_left,
                                                unsigned list_bit)
{
    avc_mv_candidate_t cand_a;
    avc_mv_candidate_t cand_b;
    avc_mv_candidate_t cand_c;
    unsigned ref_idx = 0;

    geometry_candidates(pred, partition, left, top, top_right, top_left,
                        &cand_a, &cand_b, &cand_c);
    avc_mb_direct_spatial_ref_idx(direct_candidate(cand_a),
                                  direct_candidate(cand_b),
                                  direct_candidate(cand_c),
                                  list_bit,
                                  &ref_idx);
    return ref_idx;
}

static void derive_direct_spatial_mv_l0_geometry(avc_mb_pred_event_t *pred,
                                                 unsigned partition,
                                                 const avc_mb_state_t *left,
                                                 const avc_mb_state_t *top,
                                                 const avc_mb_state_t *top_right,
                                                 const avc_mb_state_t *top_left)
{
    avc_mv_candidate_t cand_a;
    avc_mv_candidate_t cand_b;
    avc_mv_candidate_t cand_c;
    int16_t a[2] = {0, 0};
    int16_t b[2] = {0, 0};
    int16_t c[2] = {0, 0};
    unsigned ref_idx;
    int have_a;
    int have_b;
    int have_c;

    if (!pred || partition >= pred->partition_count ||
        (pred->list_mask[partition] & 1u) == 0) {
        return;
    }
    ref_idx = pred->ref_idx_l0[partition];
    geometry_candidates(pred, partition, left, top, top_right, top_left,
                        &cand_a, &cand_b, &cand_c);
    have_a = candidate_mv_l0(cand_a, ref_idx, a);
    have_b = candidate_mv_l0(cand_b, ref_idx, b);
    have_c = candidate_mv_l0(cand_c, ref_idx, c);
    avc_mb_derive_direct_spatial_mv(
        partition, pred->partition_width[partition], pred->partition_height[partition],
        (avc_mv_predictor_candidate_t){have_a, {a[0], a[1]}},
        (avc_mv_predictor_candidate_t){have_b, {b[0], b[1]}},
        (avc_mv_predictor_candidate_t){have_c, {c[0], c[1]}},
        pred->mv_pred_l0[partition], pred->mv_l0[partition]);
}

static void derive_direct_spatial_mv_l1_geometry(avc_mb_pred_event_t *pred,
                                                 unsigned partition,
                                                 const avc_mb_state_t *left,
                                                 const avc_mb_state_t *top,
                                                 const avc_mb_state_t *top_right,
                                                 const avc_mb_state_t *top_left)
{
    avc_mv_candidate_t cand_a;
    avc_mv_candidate_t cand_b;
    avc_mv_candidate_t cand_c;
    int16_t a[2] = {0, 0};
    int16_t b[2] = {0, 0};
    int16_t c[2] = {0, 0};
    unsigned ref_idx;
    int have_a;
    int have_b;
    int have_c;

    if (!pred || partition >= pred->partition_count ||
        (pred->list_mask[partition] & 2u) == 0) {
        return;
    }
    ref_idx = pred->ref_idx_l1[partition];
    geometry_candidates(pred, partition, left, top, top_right, top_left,
                        &cand_a, &cand_b, &cand_c);
    have_a = candidate_mv_l1(cand_a, ref_idx, a);
    have_b = candidate_mv_l1(cand_b, ref_idx, b);
    have_c = candidate_mv_l1(cand_c, ref_idx, c);
    avc_mb_derive_direct_spatial_mv(
        partition, pred->partition_width[partition], pred->partition_height[partition],
        (avc_mv_predictor_candidate_t){have_a, {a[0], a[1]}},
        (avc_mv_predictor_candidate_t){have_b, {b[0], b[1]}},
        (avc_mv_predictor_candidate_t){have_c, {c[0], c[1]}},
        pred->mv_pred_l1[partition], pred->mv_l1[partition]);
}

static void derive_direct_partition_geometry(avc_mb_pred_event_t *pred,
                                             unsigned partition,
                                             const avc_slice_header_t *slice,
                                             const avc_mb_state_t *left,
                                             const avc_mb_state_t *top,
                                             const avc_mb_state_t *top_right,
                                             const avc_mb_state_t *top_left)
{
    if (!pred || partition >= pred->partition_count || !pred->direct_flag[partition]) {
        return;
    }

    pred->list_mask[partition] = 3;
    pred->mvd_l0[partition][0] = 0;
    pred->mvd_l0[partition][1] = 0;
    pred->mvd_l1[partition][0] = 0;
    pred->mvd_l1[partition][1] = 0;

    if (avc_mb_direct_temporal_unsupported(slice->direct_spatial_mv_pred_flag)) {
        pred->list_mask[partition] = 0;
        pred->direct_temporal_unsupported_flag[partition] = 1;
        pred->ref_idx_l0[partition] = 0;
        pred->ref_idx_l1[partition] = 0;
        pred->mv_pred_l0[partition][0] = 0;
        pred->mv_pred_l0[partition][1] = 0;
        pred->mv_pred_l1[partition][0] = 0;
        pred->mv_pred_l1[partition][1] = 0;
        pred->mv_l0[partition][0] = 0;
        pred->mv_l0[partition][1] = 0;
        pred->mv_l1[partition][0] = 0;
        pred->mv_l1[partition][1] = 0;
        return;
    }

    pred->ref_idx_l0[partition] =
        direct_spatial_ref_idx_geometry(pred, partition, left, top, top_right,
                                        top_left, 1);
    pred->ref_idx_l1[partition] =
        direct_spatial_ref_idx_geometry(pred, partition, left, top, top_right,
                                        top_left, 2);
    derive_direct_spatial_mv_l0_geometry(pred, partition, left, top, top_right, top_left);
    derive_direct_spatial_mv_l1_geometry(pred, partition, left, top, top_right, top_left);
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
                                       const avc_mb_state_t *top_right,
                                       const avc_mb_state_t *top_left,
                                       avc_macroblock_callbacks_t callbacks,
                                       void *opaque,
                                       avc_mb_pred_event_t *out_pred)
{
    avc_mb_pred_event_t pred;
    avc_sub_mvd_grid_t l0_grid;
    unsigned i;
    unsigned partitions = avc_p_inter_partition_count(shape);
    int force_ref0 = shape == AVC_P_MB_8X8REF0;

    pred = (avc_mb_pred_event_t){0};
    l0_grid = (avc_sub_mvd_grid_t){0};
    pred.mb_address = mb_addr;
    pred.kind = AVC_MB_PRED_INTER;
    pred.partition_count = partitions;
    for (i = 0; i < partitions; i++) {
        pred.list_mask[i] = 1;
    }

    if (shape == AVC_P_MB_8X8 || shape == AVC_P_MB_8X8REF0) {
        for (i = 0; i < 4; i++) {
            trace_mb(callbacks, opaque, mb_addr,
                     "CABAC trace mb=%u enter P sub_mb_type partition=%u bit=%zu range=%u offset=%u",
                     mb_addr, i, cabac->bit_pos, cabac->cod_i_range, cabac->cod_i_offset);
            if (!avc_cabac_decode_sub_mb_type_p(cabac, &pred.sub_mb_type[i])) {
                notef(callbacks, opaque,
                      "CABAC P inter sub_mb_type decode failed mb=%u partition=%u bit=%zu",
                      mb_addr, i, cabac->bit_pos);
                return 0;
            }
            trace_mb(callbacks, opaque, mb_addr,
                     "CABAC trace mb=%u exit P sub_mb_type partition=%u value=%u bit=%zu range=%u offset=%u",
                     mb_addr, i, pred.sub_mb_type[i], cabac->bit_pos,
                     cabac->cod_i_range, cabac->cod_i_offset);
        }
    }
    avc_p_mb_set_partition_geometry(&pred, shape);

    if (!force_ref0 && slice->num_ref_idx_l0_active_minus1 > 0) {
        for (i = 0; i < partitions; i++) {
            int left_current;
            int top_current;
            unsigned left_partition;
            unsigned top_partition;
            int top_right_current;
            unsigned top_right_partition;
            unsigned left_ref;
            unsigned top_ref;

            avc_p_mb_partition_neighbors(shape, i, &left_current, &left_partition,
                                  &top_current, &top_partition,
                                  &top_right_current, &top_right_partition);
            left_ref = left_current ? pred_ref_idx_l0(&pred, left_partition) :
                                      neighbor_ref_idx_l0(left, left_partition);
            top_ref = top_current ? pred_ref_idx_l0(&pred, top_partition) :
                                    neighbor_ref_idx_l0(top, top_partition);
            {
                avc_p_inter_cabac_context_t spec_context;
                avc_mb_p_inter_cabac_context_l0(&pred, i, state_pred_l0(left),
                                                state_pred_l0(top), &spec_context);
                trace_mb(callbacks, opaque, mb_addr,
                         "CABAC trace mb=%u enter P ref_idx_l0 partition=%u legacy_left_ref=%u legacy_top_ref=%u spec_left_ref=%u spec_top_ref=%u bit=%zu range=%u offset=%u",
                         mb_addr, i, left_ref, top_ref,
                         spec_context.left_ref_idx_l0, spec_context.top_ref_idx_l0,
                         cabac->bit_pos, cabac->cod_i_range, cabac->cod_i_offset);
            }
            if (!avc_cabac_decode_ref_idx_l0_bounded(cabac, left_ref != 0, top_ref != 0,
                                                     slice->num_ref_idx_l0_active_minus1,
                                                     &pred.ref_idx_l0[i])) {
                avc_p_inter_cabac_context_t spec_context;
                avc_mb_p_inter_cabac_context_l0(&pred, i, state_pred_l0(left),
                                                state_pred_l0(top), &spec_context);
                notef(callbacks, opaque,
                      "CABAC P inter ref_idx_l0 decode failed mb=%u partition=%u legacy_left_ref=%u legacy_top_ref=%u spec_left_ref=%u spec_top_ref=%u bit=%zu",
                      mb_addr, i, left_ref, top_ref,
                      spec_context.left_ref_idx_l0, spec_context.top_ref_idx_l0,
                      cabac->bit_pos);
                return 0;
            }
            trace_mb(callbacks, opaque, mb_addr,
                     "CABAC trace mb=%u exit P ref_idx_l0 partition=%u value=%u bit=%zu range=%u offset=%u",
                     mb_addr, i, pred.ref_idx_l0[i], cabac->bit_pos,
                     cabac->cod_i_range, cabac->cod_i_offset);
        }
    } else {
        trace_mb(callbacks, opaque, mb_addr,
                 "CABAC trace mb=%u P ref_idx_l0 not present force_ref0=%d num_ref_idx_l0_active_minus1=%u",
                 mb_addr, force_ref0, slice->num_ref_idx_l0_active_minus1);
    }

    for (i = 0; i < partitions; i++) {
        int left_current;
        int top_current;
        int top_right_current;
        unsigned left_partition;
        unsigned top_partition;
        unsigned top_right_partition;
        unsigned left_mvd_x;
        unsigned top_mvd_x;
        unsigned left_mvd_y;
        unsigned top_mvd_y;
        unsigned sub_count = pred.sub_partition_count[i] ? pred.sub_partition_count[i] : 1u;
        unsigned sub;

        avc_p_mb_partition_neighbors(shape, i, &left_current, &left_partition,
                              &top_current, &top_partition,
                              &top_right_current, &top_right_partition);
        left_mvd_x = left_current ? pred_abs_mvd_l0(&pred, left_partition, 0) :
                                    neighbor_abs_mvd_l0(left, left_partition, 0);
        top_mvd_x = top_current ? pred_abs_mvd_l0(&pred, top_partition, 0) :
                                  neighbor_abs_mvd_l0(top, top_partition, 0);
        left_mvd_y = left_current ? pred_abs_mvd_l0(&pred, left_partition, 1) :
                                    neighbor_abs_mvd_l0(left, left_partition, 1);
        top_mvd_y = top_current ? pred_abs_mvd_l0(&pred, top_partition, 1) :
                                  neighbor_abs_mvd_l0(top, top_partition, 1);

        for (sub = 0; sub < sub_count; sub++) {
            int16_t mvd[2] = {0, 0};
            unsigned sub_x;
            unsigned sub_y;
            unsigned sub_width;
            unsigned sub_height;
            int have_grid_left = 0;
            int have_grid_top = 0;
            unsigned grid_left_x;
            unsigned grid_top_y;

            sub_partition_position(&pred, i, sub, &sub_x, &sub_y,
                                   &sub_width, &sub_height);
            if (sub_x > 0) {
                grid_left_x = sub_x - 1u;
                left_mvd_x = sub_mvd_grid_abs_at(&l0_grid, grid_left_x, sub_y, 0, &have_grid_left);
                left_mvd_y = sub_mvd_grid_abs_at(&l0_grid, grid_left_x, sub_y, 1, &have_grid_left);
            }
            if (sub_y > 0) {
                grid_top_y = sub_y - 1u;
                top_mvd_x = sub_mvd_grid_abs_at(&l0_grid, sub_x, grid_top_y, 0, &have_grid_top);
                top_mvd_y = sub_mvd_grid_abs_at(&l0_grid, sub_x, grid_top_y, 1, &have_grid_top);
            }

            trace_mb(callbacks, opaque, mb_addr,
                     "CABAC trace mb=%u enter P mvd_l0_x partition=%u sub=%u legacy_left_abs=%u legacy_top_abs=%u bit=%zu range=%u offset=%u",
                     mb_addr, i, sub, left_mvd_x, top_mvd_x,
                     cabac->bit_pos, cabac->cod_i_range, cabac->cod_i_offset);
            if (!avc_cabac_decode_mvd_component(cabac, 40, left_mvd_x, top_mvd_x,
                                                &mvd[0])) {
                avc_p_inter_cabac_context_t spec_context;
                avc_mb_p_inter_cabac_context_l0(&pred, i, state_pred_l0(left),
                                                state_pred_l0(top), &spec_context);
                notef(callbacks, opaque,
                      "CABAC P inter mvd_l0_x decode failed mb=%u partition=%u sub=%u legacy_left_abs=%u legacy_top_abs=%u spec_left_abs=%u spec_top_abs=%u bit=%zu",
                      mb_addr, i, sub, left_mvd_x, top_mvd_x,
                      spec_context.left_abs_mvd_l0[0], spec_context.top_abs_mvd_l0[0],
                      cabac->bit_pos);
                return 0;
            }
            trace_mb(callbacks, opaque, mb_addr,
                     "CABAC trace mb=%u exit P mvd_l0_x partition=%u sub=%u value=%d bit=%zu range=%u offset=%u",
                     mb_addr, i, sub, mvd[0], cabac->bit_pos,
                     cabac->cod_i_range, cabac->cod_i_offset);
            trace_mb(callbacks, opaque, mb_addr,
                     "CABAC trace mb=%u enter P mvd_l0_y partition=%u sub=%u legacy_left_abs=%u legacy_top_abs=%u bit=%zu range=%u offset=%u",
                     mb_addr, i, sub, left_mvd_y, top_mvd_y,
                     cabac->bit_pos, cabac->cod_i_range, cabac->cod_i_offset);
            if (!avc_cabac_decode_mvd_component(cabac, 47, left_mvd_y, top_mvd_y,
                                                &mvd[1])) {
                avc_p_inter_cabac_context_t spec_context;
                avc_mb_p_inter_cabac_context_l0(&pred, i, state_pred_l0(left),
                                                state_pred_l0(top), &spec_context);
                notef(callbacks, opaque,
                      "CABAC P inter mvd_l0_y decode failed mb=%u partition=%u sub=%u legacy_left_abs=%u legacy_top_abs=%u spec_left_abs=%u spec_top_abs=%u bit=%zu",
                      mb_addr, i, sub, left_mvd_y, top_mvd_y,
                      spec_context.left_abs_mvd_l0[1], spec_context.top_abs_mvd_l0[1],
                      cabac->bit_pos);
                return 0;
            }
            trace_mb(callbacks, opaque, mb_addr,
                     "CABAC trace mb=%u exit P mvd_l0_y partition=%u sub=%u value=%d bit=%zu range=%u offset=%u",
                     mb_addr, i, sub, mvd[1], cabac->bit_pos,
                     cabac->cod_i_range, cabac->cod_i_offset);
            if (sub == 0) {
                pred.mvd_l0[i][0] = mvd[0];
                pred.mvd_l0[i][1] = mvd[1];
            }
            sub_mvd_grid_store(&l0_grid, sub_x, sub_y, sub_width, sub_height, mvd);
        }
        derive_partition_mv_l0(&pred, i, shape, left, top, top_right, top_left);
    }

    avc_mb_populate_sub_partition_motion(&pred);
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
                                       const avc_mb_state_t *top_right,
                                       const avc_mb_state_t *top_left,
                                       avc_macroblock_callbacks_t callbacks,
                                       void *opaque,
                                       avc_mb_pred_event_t *out_pred)
{
    avc_mb_pred_event_t pred;
    avc_sub_mvd_grid_t l0_grid;
    avc_sub_mvd_grid_t l1_grid;
    unsigned i;

    pred = (avc_mb_pred_event_t){0};
    l0_grid = (avc_sub_mvd_grid_t){0};
    l1_grid = (avc_sub_mvd_grid_t){0};
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
            pred.direct_flag[i] = (uint8_t)avc_b_sub_mb_is_direct(pred.sub_mb_type[i]);
            pred.list_mask[i] = avc_b_sub_mb_list_mask(pred.sub_mb_type[i]);
        }
    } else {
        for (i = 0; i < info.partition_count; i++) {
            pred.list_mask[i] = info.list_mask[i];
        }
    }
    avc_b_mb_set_partition_geometry(&pred, &info);

    if (slice->num_ref_idx_l0_active_minus1 > 0) {
        for (i = 0; i < pred.partition_count; i++) {
            avc_mv_candidate_t cand_a;
            avc_mv_candidate_t cand_b;
            unsigned left_ref;
            unsigned top_ref;

            if ((pred.list_mask[i] & 1u) == 0) {
                continue;
            }
            cand_a = mv_candidate_a(&pred, i, left);
            cand_b = mv_candidate_b(&pred, i, top);
            left_ref = candidate_ref_idx_l0(cand_a);
            top_ref = candidate_ref_idx_l0(cand_b);
            if (!avc_cabac_decode_ref_idx_l0_bounded(cabac, left_ref != 0, top_ref != 0,
                                                     slice->num_ref_idx_l0_active_minus1,
                                                     &pred.ref_idx_l0[i])) {
                return 0;
            }
        }
    }
    if (slice->num_ref_idx_l1_active_minus1 > 0) {
        for (i = 0; i < pred.partition_count; i++) {
            avc_mv_candidate_t cand_a;
            avc_mv_candidate_t cand_b;
            unsigned left_ref;
            unsigned top_ref;

            if ((pred.list_mask[i] & 2u) == 0) {
                continue;
            }
            cand_a = mv_candidate_a(&pred, i, left);
            cand_b = mv_candidate_b(&pred, i, top);
            left_ref = candidate_ref_idx_l1(cand_a);
            top_ref = candidate_ref_idx_l1(cand_b);
            if (!avc_cabac_decode_ref_idx_l1_bounded(cabac, left_ref != 0, top_ref != 0,
                                                     slice->num_ref_idx_l1_active_minus1,
                                                     &pred.ref_idx_l1[i])) {
                return 0;
            }
        }
    }

    for (i = 0; i < pred.partition_count; i++) {
        avc_mv_candidate_t cand_a = mv_candidate_a(&pred, i, left);
        avc_mv_candidate_t cand_b = mv_candidate_b(&pred, i, top);

        if (pred.direct_flag[i]) {
            derive_direct_partition_geometry(&pred, i, slice, left, top,
                                             top_right, top_left);
            continue;
        }
        if (pred.list_mask[i] & 1u) {
            unsigned left_x = candidate_abs_mvd_l0(cand_a, 0);
            unsigned top_x = candidate_abs_mvd_l0(cand_b, 0);
            unsigned left_y = candidate_abs_mvd_l0(cand_a, 1);
            unsigned top_y = candidate_abs_mvd_l0(cand_b, 1);
            unsigned sub_count = pred.sub_partition_count[i] ? pred.sub_partition_count[i] : 1u;
            unsigned sub;

            for (sub = 0; sub < sub_count; sub++) {
                int16_t mvd[2] = {0, 0};
                unsigned sub_x;
                unsigned sub_y;
                unsigned sub_width;
                unsigned sub_height;
                int have_grid_left = 0;
                int have_grid_top = 0;

                sub_partition_position(&pred, i, sub, &sub_x, &sub_y,
                                       &sub_width, &sub_height);
                if (sub_x > 0) {
                    left_x = sub_mvd_grid_abs_at(&l0_grid, sub_x - 1u, sub_y, 0, &have_grid_left);
                    left_y = sub_mvd_grid_abs_at(&l0_grid, sub_x - 1u, sub_y, 1, &have_grid_left);
                }
                if (sub_y > 0) {
                    top_x = sub_mvd_grid_abs_at(&l0_grid, sub_x, sub_y - 1u, 0, &have_grid_top);
                    top_y = sub_mvd_grid_abs_at(&l0_grid, sub_x, sub_y - 1u, 1, &have_grid_top);
                }
                if (!avc_cabac_decode_mvd_component(cabac, 40, left_x, top_x, &mvd[0]) ||
                    !avc_cabac_decode_mvd_component(cabac, 47, left_y, top_y, &mvd[1])) {
                    return 0;
                }
                if (sub == 0) {
                    pred.mvd_l0[i][0] = mvd[0];
                    pred.mvd_l0[i][1] = mvd[1];
                }
                sub_mvd_grid_store(&l0_grid, sub_x, sub_y, sub_width, sub_height, mvd);
            }
            derive_partition_mv_l0_geometry(&pred, i, left, top, top_right, top_left);
        }
        if (pred.list_mask[i] & 2u) {
            unsigned left_x = candidate_abs_mvd_l1(cand_a, 0);
            unsigned top_x = candidate_abs_mvd_l1(cand_b, 0);
            unsigned left_y = candidate_abs_mvd_l1(cand_a, 1);
            unsigned top_y = candidate_abs_mvd_l1(cand_b, 1);
            unsigned sub_count = pred.sub_partition_count[i] ? pred.sub_partition_count[i] : 1u;
            unsigned sub;

            for (sub = 0; sub < sub_count; sub++) {
                int16_t mvd[2] = {0, 0};
                unsigned sub_x;
                unsigned sub_y;
                unsigned sub_width;
                unsigned sub_height;
                int have_grid_left = 0;
                int have_grid_top = 0;

                sub_partition_position(&pred, i, sub, &sub_x, &sub_y,
                                       &sub_width, &sub_height);
                if (sub_x > 0) {
                    left_x = sub_mvd_grid_abs_at(&l1_grid, sub_x - 1u, sub_y, 0, &have_grid_left);
                    left_y = sub_mvd_grid_abs_at(&l1_grid, sub_x - 1u, sub_y, 1, &have_grid_left);
                }
                if (sub_y > 0) {
                    top_x = sub_mvd_grid_abs_at(&l1_grid, sub_x, sub_y - 1u, 0, &have_grid_top);
                    top_y = sub_mvd_grid_abs_at(&l1_grid, sub_x, sub_y - 1u, 1, &have_grid_top);
                }
                if (!avc_cabac_decode_mvd_component(cabac, 40, left_x, top_x, &mvd[0]) ||
                    !avc_cabac_decode_mvd_component(cabac, 47, left_y, top_y, &mvd[1])) {
                    return 0;
                }
                if (sub == 0) {
                    pred.mvd_l1[i][0] = mvd[0];
                    pred.mvd_l1[i][1] = mvd[1];
                }
                sub_mvd_grid_store(&l1_grid, sub_x, sub_y, sub_width, sub_height, mvd);
            }
            derive_partition_mv_l1_geometry(&pred, i, left, top, top_right, top_left);
        }
    }

    avc_mb_populate_sub_partition_motion(&pred);
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
                               const avc_mb_state_t *left,
                               const avc_mb_state_t *top,
                               const avc_mb_state_t *top_right,
                               const avc_mb_state_t *top_left,
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
    avc_mb_set_partition_geometry(&pred, 0, 0, 0, 16, 16, 1, 16, 16);
    derive_direct_partition_geometry(&pred, 0, slice, left, top,
                                     top_right, top_left);
    avc_mb_populate_sub_partition_motion(&pred);
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
                             const avc_mb_state_t *top_right,
                             const avc_mb_state_t *top_left,
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
    avc_mb_set_partition_geometry(&pred, 0, 0, 0, 16, 16, 1, 16, 16);
    {
        int16_t mv_a[2] = {0, 0};
        int16_t mv_b[2] = {0, 0};
        int16_t mv_c[2] = {0, 0};
        avc_mv_predictor_candidate_t a;
        avc_mv_predictor_candidate_t b;
        avc_mv_predictor_candidate_t c;

        a.available = neighbor_mv_l0(left, 0, 0, mv_a);
        a.mv[0] = mv_a[0];
        a.mv[1] = mv_a[1];
        b.available = neighbor_mv_l0(top, 0, 0, mv_b);
        b.mv[0] = mv_b[0];
        b.mv[1] = mv_b[1];
        c.available = neighbor_mv_l0(top_right, 0, 0, mv_c);
        if (!c.available) {
            c.available = neighbor_mv_l0(top_left, 0, 0, mv_c);
        }
        c.mv[0] = mv_c[0];
        c.mv[1] = mv_c[1];

        avc_mb_derive_p_skip_mv(a, b, c, pred.mv_pred_l0[0]);
        pred.mv_l0[0][0] = pred.mv_pred_l0[0][0];
        pred.mv_l0[0][1] = pred.mv_pred_l0[0][1];
    }
    avc_mb_populate_sub_partition_motion(&pred);
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

static int cabac_fill_coeff_coords(avc_residual_event_t *residual,
                                   unsigned chroma_format,
                                   avc_cavlc_scan_t scan_mode)
{
    unsigned i;
    unsigned origin_x = 0;
    unsigned origin_y = 0;
    unsigned ac_blocks;

    for (i = 0; i < residual->cabac_block.max_coeff; i++) {
        if (residual->cabac_block.coeff_level[i] == 0) {
            continue;
        }
        if (!avc_cavlc_scan_position(residual->cabac_block.max_coeff, i,
                                     scan_mode,
                                     &residual->cabac_block.coeff_x[i],
                                     &residual->cabac_block.coeff_y[i])) {
            return 0;
        }
        switch (residual->block_kind) {
        case AVC_RESIDUAL_LUMA_4X4:
        case AVC_RESIDUAL_LUMA_16X16_AC:
            if (!avc_mb_luma4x4_block_origin(residual->block_index, &origin_x, &origin_y)) {
                return 0;
            }
            break;
        case AVC_RESIDUAL_LUMA_8X8:
            if (!avc_mb_luma8x8_block_origin(residual->block_index, &origin_x, &origin_y)) {
                return 0;
            }
            break;
        case AVC_RESIDUAL_LUMA_16X16_DC:
            origin_x = 0;
            origin_y = 0;
            residual->coeff_mb_x[i] = residual->cabac_block.coeff_x[i] * 4u;
            residual->coeff_mb_y[i] = residual->cabac_block.coeff_y[i] * 4u;
            continue;
        case AVC_RESIDUAL_CHROMA_AC:
            ac_blocks = avc_mb_chroma_ac_blocks_per_component(chroma_format);
            if (ac_blocks == 0 ||
                !avc_mb_chroma4x4_block_origin(chroma_format, residual->block_index % ac_blocks,
                                        &origin_x, &origin_y)) {
                return 0;
            }
            break;
        case AVC_RESIDUAL_CHROMA_DC:
            residual->coeff_mb_x[i] = residual->cabac_block.coeff_x[i] * 4u;
            residual->coeff_mb_y[i] = residual->cabac_block.coeff_y[i] * 4u;
            continue;
        default:
            return 0;
        }
        residual->coeff_mb_x[i] = origin_x + residual->cabac_block.coeff_x[i];
        residual->coeff_mb_y[i] = origin_y + residual->cabac_block.coeff_y[i];
    }
    return 1;
}

static int cabac_emit_residual_block(avc_cabac_decoder_t *cabac,
                                     uint32_t mb_addr,
                                     avc_residual_kind_t kind,
                                     unsigned block_index,
                                     unsigned max_coeff_override,
                                     int left_coded,
                                     int top_coded,
                                     const avc_pps_t *pps,
                                     const avc_sps_t *sps,
                                     const avc_macroblock_event_t *event,
                                     avc_mb_pred_kind_t pred_kind,
                                     unsigned chroma_format,
                                     avc_cavlc_scan_t scan_mode,
                                     avc_mb_state_t *curr,
                                     avc_macroblock_callbacks_t callbacks,
                                     void *opaque)
{
    const avc_cabac_residual_plan_t *plan = residual_plan(kind);
    avc_residual_event_t residual;
    unsigned max_coeff;
    int coded_block_flag_present;

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
    coded_block_flag_present = max_coeff != 64 || chroma_format == 3;

    if (!avc_cabac_decode_residual_block(cabac, max_coeff,
                                         plan->ctx_block_cat,
                                         coded_block_flag_present,
                                         plan->coded_ctx_base,
                                         plan->sig_ctx_base,
                                         plan->last_ctx_base,
                                         plan->level_ctx_base,
                                         left_coded, top_coded,
                                         scan_mode == AVC_CAVLC_SCAN_FIELD ||
                                             scan_mode == AVC_CAVLC_SCAN_TRANSFORM_BYPASS_FIELD,
                                         &residual.cabac_block)) {
        notef(callbacks, opaque,
              "CABAC residual parse failed syntax=%s mb=%u kind=%u block=%u max_coeff=%u index=%u ctx=%u bit=%zu",
              cabac_residual_error_name(residual.cabac_block.error_syntax),
              mb_addr, (unsigned)kind, block_index, max_coeff,
              residual.cabac_block.error_index,
              residual.cabac_block.error_context,
              cabac->bit_pos);
        return 0;
    }
    if (!cabac_fill_coeff_coords(&residual, chroma_format, scan_mode)) {
        notef(callbacks, opaque,
              "CABAC coefficient coordinate mapping failed mb=%u kind=%u block=%u",
              mb_addr, (unsigned)kind, block_index);
        cabac->error = 1;
        return 0;
    }
    store_residual_nonzero(curr, kind, block_index, residual.cabac_block.total_coeff,
                           chroma_format);
    fill_residual_metadata(&residual, pps, sps, event, pred_kind);
    if (callbacks.on_residual) {
        callbacks.on_residual(opaque, &residual);
    }
    return !cabac->error;
}

static int cabac_emit_luma_residuals(avc_cabac_decoder_t *cabac,
                                     uint32_t mb_addr,
                                     const avc_pps_t *pps,
                                     const avc_sps_t *sps,
                                     const avc_macroblock_event_t *event,
                                     avc_mb_pred_kind_t pred_kind,
                                     uint32_t cbp_luma,
                                     int transform_size_8x8_flag,
                                     avc_cavlc_scan_t scan_mode,
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
                                       0, 0, left_coded, top_coded, pps, sps, event,
                                       pred_kind, 0, scan_mode, curr,
                                       callbacks, opaque)) {
            return 0;
        }
        if (!cbp_luma) {
            return 1;
        }
        for (group = 0; group < 16; group++) {
            cabac_luma_cbf_neighbors(curr, left, top, pred_kind, group, 0,
                                     &left_coded, &top_coded);
            if (!cabac_emit_residual_block(cabac, mb_addr, AVC_RESIDUAL_LUMA_16X16_AC,
                                           group, 0, left_coded, top_coded, pps, sps, event,
                                           pred_kind, 0, scan_mode, curr,
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
            cabac_luma_cbf_neighbors(curr, left, top, pred_kind, group, 1,
                                     &left_coded, &top_coded);
            if (!cabac_emit_residual_block(cabac, mb_addr, AVC_RESIDUAL_LUMA_8X8,
                                           group, 0, left_coded, top_coded, pps, sps, event,
                                           pred_kind, 0, scan_mode, curr,
                                           callbacks, opaque)) {
                return 0;
            }
        } else {
            for (sub = 0; sub < 4; sub++) {
                unsigned block_index = group * 4u + sub;
                cabac_luma_cbf_neighbors(curr, left, top, pred_kind, block_index, 0,
                                         &left_coded, &top_coded);
                if (!cabac_emit_residual_block(cabac, mb_addr, AVC_RESIDUAL_LUMA_4X4,
                                               block_index, 0, left_coded, top_coded, pps, sps, event,
                                               pred_kind, 0, scan_mode, curr,
                                               callbacks, opaque)) {
                    return 0;
                }
            }
        }
    }
    return !cabac->error;
}

static int cabac_emit_chroma_residuals(avc_cabac_decoder_t *cabac,
                                       uint32_t mb_addr,
                                       const avc_pps_t *pps,
                                       const avc_sps_t *sps,
                                       const avc_macroblock_event_t *event,
                                       avc_mb_pred_kind_t pred_kind,
                                       uint32_t cbp_chroma,
                                       avc_cavlc_scan_t scan_mode,
                                       const avc_mb_state_t *left,
                                       const avc_mb_state_t *top,
                                       avc_mb_state_t *curr,
                                       avc_macroblock_callbacks_t callbacks,
                                       void *opaque)
{
    unsigned chroma_format = avc_mb_chroma_array_type(sps);
    unsigned dc_coeffs = avc_mb_chroma_dc_coeffs(chroma_format);
    unsigned ac_blocks = avc_mb_chroma_ac_blocks_per_component(chroma_format);
    unsigned component;

    if (cbp_chroma == 0 || chroma_format == 0) {
        return 1;
    }

    for (component = 0; component < 2; component++) {
        int left_coded;
        int top_coded;
        cabac_chroma_dc_cbf_neighbors(left, top, pred_kind, component, &left_coded, &top_coded);
        if (!cabac_emit_residual_block(cabac, mb_addr, AVC_RESIDUAL_CHROMA_DC,
                                       component, dc_coeffs, left_coded, top_coded,
                                       pps, sps, event, pred_kind, chroma_format,
                                       scan_mode, curr, callbacks, opaque)) {
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
            cabac_chroma_ac_cbf_neighbors(curr, left, top, pred_kind, chroma_format,
                                          component, block, &left_coded, &top_coded);
            if (!cabac_emit_residual_block(cabac, mb_addr, AVC_RESIDUAL_CHROMA_AC,
                                           block_index, 0, left_coded, top_coded,
                                           pps, sps, event, pred_kind, chroma_format,
                                           scan_mode, curr, callbacks, opaque)) {
                return 0;
            }
        }
    }
    return !cabac->error;
}

static int cabac_consume_ipcm(avc_cabac_decoder_t *cabac,
                              const avc_sps_t *sps,
                              avc_macroblock_event_t *event,
                              unsigned *pcm_bit_pos,
                              unsigned *backup_bytes_out)
{
    unsigned chroma_format = avc_mb_chroma_array_type(sps);
    unsigned luma_bits = sps->bit_depth_luma_minus8 + 8u;
    unsigned chroma_bits = sps->bit_depth_chroma_minus8 + 8u;
    uint32_t chroma_samples_per_component = 0;
    size_t pcm_offset;
    size_t pcm_size_bits;
    size_t pcm_size_bytes;
    unsigned backup_bytes = (cabac->cod_i_offset & 1u) ? 1u : 0u;

    if (luma_bits > 32 || chroma_bits > 32) {
        cabac->error = 1;
        return 0;
    }
    if (pcm_bit_pos) {
        *pcm_bit_pos = 0;
    }
    if (backup_bytes_out) {
        *backup_bytes_out = 0;
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

    /*
     * After the terminating I_PCM bin, the arithmetic decoder may have already
     * prefetched bits from the raw PCM payload. Resume from the raw byte
     * boundary before those lookahead bits, then reinitialize CABAC after the
     * PCM sample bytes.
     */
    pcm_offset = (cabac->bit_pos + 7u) >> 3;
    if (backup_bytes > pcm_offset) {
        cabac->error = 1;
        return 0;
    }
    pcm_offset -= backup_bytes;

    pcm_size_bits = (size_t)event->pcm_luma_samples * luma_bits +
                    (size_t)event->pcm_chroma_samples * chroma_bits;
    if ((pcm_size_bits & 7u) != 0) {
        cabac->error = 1;
        return 0;
    }
    pcm_size_bytes = pcm_size_bits >> 3;
    if (pcm_offset > cabac->size || pcm_size_bytes > cabac->size - pcm_offset) {
        cabac->error = 1;
        return 0;
    }

    avc_cabac_init(cabac, cabac->data + pcm_offset + pcm_size_bytes,
                   cabac->size - pcm_offset - pcm_size_bytes);
    if (pcm_bit_pos) {
        *pcm_bit_pos = (unsigned)(pcm_offset * 8u);
    }
    if (backup_bytes_out) {
        *backup_bytes_out = backup_bytes;
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
                                       const avc_mb_state_t *top_right,
                                       const avc_mb_state_t *top_left,
                                       avc_macroblock_callbacks_t callbacks,
                                       void *opaque,
                                       avc_mb_pred_event_t *out_pred)
{
    avc_mb_pred_event_t pred;
    unsigned i;
    unsigned partitions = avc_p_inter_partition_count(shape);
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
    avc_p_mb_set_partition_geometry(&pred, shape);
    if (!force_ref0 && slice->num_ref_idx_l0_active_minus1 > 0) {
        for (i = 0; i < partitions; i++) {
            pred.ref_idx_l0[i] = avc_br_read_ue(br);
        }
    }
    for (i = 0; i < partitions; i++) {
        pred.mvd_l0[i][0] = (int16_t)avc_br_read_se(br);
        pred.mvd_l0[i][1] = (int16_t)avc_br_read_se(br);
        derive_partition_mv_l0(&pred, i, shape, left, top, top_right, top_left);
    }
    avc_mb_populate_sub_partition_motion(&pred);
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
                                       const avc_mb_state_t *top_right,
                                       const avc_mb_state_t *top_left,
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
            pred.direct_flag[i] = (uint8_t)avc_b_sub_mb_is_direct(pred.sub_mb_type[i]);
            pred.list_mask[i] = avc_b_sub_mb_list_mask(pred.sub_mb_type[i]);
        }
    } else {
        for (i = 0; i < info.partition_count; i++) {
            pred.list_mask[i] = info.list_mask[i];
        }
    }
    avc_b_mb_set_partition_geometry(&pred, &info);

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
        if (pred.direct_flag[i]) {
            derive_direct_partition_geometry(&pred, i, slice, left, top,
                                             top_right, top_left);
            continue;
        }
        if (pred.list_mask[i] & 1u) {
            pred.mvd_l0[i][0] = (int16_t)avc_br_read_se(br);
            pred.mvd_l0[i][1] = (int16_t)avc_br_read_se(br);
            derive_partition_mv_l0_geometry(&pred, i, left, top, top_right, top_left);
        }
        if (pred.list_mask[i] & 2u) {
            pred.mvd_l1[i][0] = (int16_t)avc_br_read_se(br);
            pred.mvd_l1[i][1] = (int16_t)avc_br_read_se(br);
            derive_partition_mv_l1_geometry(&pred, i, left, top, top_right, top_left);
        }
    }

    avc_mb_populate_sub_partition_motion(&pred);
    if (out_pred) {
        *out_pred = pred;
    }
    if (callbacks.on_mb_pred) {
        callbacks.on_mb_pred(opaque, &pred);
    }
    return !br->error;
}

static int cavlc_fill_macroblock_coeff_coords(avc_residual_event_t *residual,
                                              unsigned chroma_format)
{
    unsigned i;
    unsigned origin_x = 0;
    unsigned origin_y = 0;
    unsigned ac_blocks;

    for (i = 0; i < residual->block.total_coeff; i++) {
        switch (residual->block_kind) {
        case AVC_RESIDUAL_LUMA_4X4:
        case AVC_RESIDUAL_LUMA_8X8:
        case AVC_RESIDUAL_LUMA_16X16_AC:
            if (!avc_mb_luma4x4_block_origin(residual->block_index, &origin_x, &origin_y)) {
                return 0;
            }
            residual->coeff_mb_x[i] = origin_x + residual->block.coeff_x[i];
            residual->coeff_mb_y[i] = origin_y + residual->block.coeff_y[i];
            break;
        case AVC_RESIDUAL_LUMA_16X16_DC:
            residual->coeff_mb_x[i] = residual->block.coeff_x[i] * 4u;
            residual->coeff_mb_y[i] = residual->block.coeff_y[i] * 4u;
            break;
        case AVC_RESIDUAL_CHROMA_AC:
            ac_blocks = avc_mb_chroma_ac_blocks_per_component(chroma_format);
            if (ac_blocks == 0 ||
                !avc_mb_chroma4x4_block_origin(chroma_format, residual->block_index % ac_blocks,
                                        &origin_x, &origin_y)) {
                return 0;
            }
            residual->coeff_mb_x[i] = origin_x + residual->block.coeff_x[i];
            residual->coeff_mb_y[i] = origin_y + residual->block.coeff_y[i];
            break;
        case AVC_RESIDUAL_CHROMA_DC:
            residual->coeff_mb_x[i] = residual->block.coeff_x[i] * 4u;
            residual->coeff_mb_y[i] = residual->block.coeff_y[i] * 4u;
            break;
        default:
            return 0;
        }
    }
    return 1;
}

static int cavlc_emit_residual_block(avc_bitreader_t *br,
                                     uint32_t mb_addr,
                                     avc_residual_kind_t kind,
                                     unsigned block_index,
                                     unsigned max_coeff,
                                     int nC,
                                     const avc_pps_t *pps,
                                     const avc_sps_t *sps,
                                     const avc_macroblock_event_t *event,
                                     avc_mb_pred_kind_t pred_kind,
                                     unsigned chroma_format,
                                     avc_cavlc_scan_t scan_mode,
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

    if (!avc_cavlc_read_residual_block(br, nC, max_coeff, scan_mode, &residual.block,
                                       cavlc_callbacks, NULL)) {
        notef(callbacks, opaque,
              "CAVLC residual parse failed mb=%u kind=%u block=%u max_coeff=%u nC=%d bit=%zu",
              mb_addr, (unsigned)kind, block_index, max_coeff, nC, br->bit_pos);
        return 0;
    }
    if (!cavlc_fill_macroblock_coeff_coords(&residual, chroma_format)) {
        notef(callbacks, opaque,
              "CAVLC coefficient coordinate mapping failed mb=%u kind=%u block=%u",
              mb_addr, (unsigned)kind, block_index);
        br->error = 1;
        return 0;
    }
    store_residual_nonzero(curr, kind, block_index, residual.block.total_coeff,
                           chroma_format);
    fill_residual_metadata(&residual, pps, sps, event, pred_kind);
    if (callbacks.on_residual) {
        callbacks.on_residual(opaque, &residual);
    }
    return !br->error;
}

static int cavlc_emit_luma_residuals(avc_bitreader_t *br,
                                     uint32_t mb_addr,
                                     const avc_pps_t *pps,
                                     const avc_sps_t *sps,
                                     const avc_macroblock_event_t *event,
                                     avc_mb_pred_kind_t pred_kind,
                                     uint32_t cbp_luma,
                                     int transform_size_8x8_flag,
                                     avc_cavlc_scan_t scan_mode,
                                     const avc_mb_state_t *left,
                                     const avc_mb_state_t *top,
                                     avc_mb_state_t *curr,
                                     avc_macroblock_callbacks_t callbacks,
                                     void *opaque)
{
    unsigned group;

    if (pred_kind == AVC_MB_PRED_INTRA_16X16) {
        if (!cavlc_emit_residual_block(br, mb_addr, AVC_RESIDUAL_LUMA_16X16_DC,
                                       0, 16, 0, pps, sps, event, pred_kind, 0,
                                       scan_mode, curr, callbacks, opaque)) {
            return 0;
        }
        if (!cbp_luma) {
            return 1;
        }
        for (group = 0; group < 16; group++) {
            if (!cavlc_emit_residual_block(br, mb_addr, AVC_RESIDUAL_LUMA_16X16_AC,
                                           group, 15,
                                           cavlc_luma_nC(curr, left, top, group),
                                           pps, sps, event, pred_kind, 0,
                                           scan_mode, curr, callbacks, opaque)) {
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
                                               pps, sps, event, pred_kind, 0,
                                               scan_mode, curr, callbacks, opaque)) {
                    return 0;
                }
            }
        } else {
            for (sub = 0; sub < 4; sub++) {
                unsigned block_index = group * 4u + sub;
                if (!cavlc_emit_residual_block(br, mb_addr, AVC_RESIDUAL_LUMA_4X4,
                                               block_index, 16,
                                               cavlc_luma_nC(curr, left, top, block_index),
                                               pps, sps, event, pred_kind, 0,
                                               scan_mode, curr, callbacks, opaque)) {
                    return 0;
                }
            }
        }
    }
    return !br->error;
}

static int cavlc_emit_chroma_residuals(avc_bitreader_t *br,
                                       uint32_t mb_addr,
                                       const avc_pps_t *pps,
                                       const avc_sps_t *sps,
                                       const avc_macroblock_event_t *event,
                                       avc_mb_pred_kind_t pred_kind,
                                       uint32_t cbp_chroma,
                                       avc_cavlc_scan_t scan_mode,
                                       const avc_mb_state_t *left,
                                       const avc_mb_state_t *top,
                                       avc_mb_state_t *curr,
                                       avc_macroblock_callbacks_t callbacks,
                                       void *opaque)
{
    unsigned chroma_format = avc_mb_chroma_array_type(sps);
    unsigned dc_coeffs = avc_mb_chroma_dc_coeffs(chroma_format);
    unsigned ac_blocks = avc_mb_chroma_ac_blocks_per_component(chroma_format);
    unsigned component;

    if (cbp_chroma == 0 || chroma_format == 0) {
        return 1;
    }
    for (component = 0; component < 2; component++) {
        if (!cavlc_emit_residual_block(br, mb_addr, AVC_RESIDUAL_CHROMA_DC,
                                       component, dc_coeffs, 0, pps, sps, event,
                                       pred_kind, chroma_format,
                                       scan_mode, curr, callbacks, opaque)) {
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
                                           pps, sps, event, pred_kind,
                                           chroma_format, scan_mode, curr,
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
    unsigned chroma_format = avc_mb_chroma_array_type(sps);
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
    uint32_t max_mbs = avc_mb_pic_size_in_mbs(sps);
    uint32_t width = avc_mb_pic_width_in_mbs(sps);
    int32_t curr_qp_y = 26 + pps->pic_init_qp_minus26 + slice->slice_qp_delta;
    int prev_mb_skipped = 0;
    avc_mb_state_t *states = (avc_mb_state_t *)calloc(max_mbs ? max_mbs : 1u, sizeof(*states));

    if (!states) {
        return 0;
    }

    while (mb_addr < max_mbs && avc_br_more_rbsp_data(br)) {
        avc_macroblock_event_t event;
        const avc_mb_state_t *left;
        const avc_mb_state_t *top;
        const avc_mb_state_t *top_right;
        const avc_mb_state_t *top_left;
        avc_mb_state_t *curr;
        avc_mb_pred_event_t pred_event;
        avc_mb_pred_event_t *pred_for_state = NULL;

        summary->next_mb_address = mb_addr;
        event = (avc_macroblock_event_t){0};
        pred_event = (avc_mb_pred_event_t){0};
        event.address = mb_addr;
        event.entropy = AVC_MB_ENTROPY_CAVLC;
        event.qp_y = curr_qp_y;
        avc_mb_fill_chroma_qp(&event, pps, sps);
        neighbor_states4(states, max_mbs, width, mb_addr, &left, &top,
                         &top_right, &top_left);
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
                skipped.mb_field_decoding_flag = mb_pair_field_flag(states, max_mbs, skipped.address);
                skipped.qp_y = curr_qp_y;
                avc_mb_fill_chroma_qp(&skipped, pps, sps);
                if (slice->slice_kind == AVC_SLICE_B) {
                    const avc_mb_state_t *skip_left;
                    const avc_mb_state_t *skip_top;
                    const avc_mb_state_t *skip_top_right;
                    const avc_mb_state_t *skip_top_left;

                    neighbor_states4(states, max_mbs, width, skipped.address,
                                     &skip_left, &skip_top,
                                     &skip_top_right, &skip_top_left);
                    emit_b_direct_pred(skipped.address, slice,
                                       skip_left, skip_top,
                                       skip_top_right, skip_top_left,
                                       callbacks, opaque, &skipped_pred);
                    skipped_pred_ptr = &skipped_pred;
                } else {
                    const avc_mb_state_t *skip_left;
                    const avc_mb_state_t *skip_top;
                    const avc_mb_state_t *skip_top_right;
                    const avc_mb_state_t *skip_top_left;

                    neighbor_states4(states, max_mbs, width, skipped.address,
                                     &skip_left, &skip_top,
                                     &skip_top_right, &skip_top_left);
                    emit_p_skip_pred(skipped.address, skip_left, skip_top,
                                     skip_top_right, skip_top_left,
                                     callbacks, opaque, &skipped_pred);
                    skipped_pred_ptr = &skipped_pred;
                }
                if (callbacks.on_macroblock) {
                    callbacks.on_macroblock(opaque, &skipped);
                }
                save_mb_state(states, max_mbs, &skipped, skipped_pred_ptr);
                summary->macroblocks_seen++;
                event.mb_skip_run--;
                prev_mb_skipped = 1;
                summary->next_mb_address = mb_addr;
            }
            if (mb_addr >= max_mbs || !avc_br_more_rbsp_data(br)) {
                break;
            }
            event.address = mb_addr;
            neighbor_states4(states, max_mbs, width, mb_addr, &left, &top,
                             &top_right, &top_left);
            curr = state_for(states, max_mbs, mb_addr);
        }

        if (mb_field_flag_present(slice, sps, mb_addr, prev_mb_skipped)) {
            event.mb_field_decoding_flag = (int)avc_br_read_bit(br);
            if (br->error) {
                free(states);
                return 0;
            }
        } else {
            event.mb_field_decoding_flag = mb_pair_field_flag(states, max_mbs, mb_addr);
        }
        prev_mb_skipped = 0;

        event.mb_type = avc_br_read_ue(br);
        if (br->error) {
            free(states);
            return 0;
        }
        if (slice->slice_kind == AVC_SLICE_I || slice->slice_kind == AVC_SLICE_SI) {
            avc_i_mb_type_info_t i_info = avc_i_mb_type_classify(event.mb_type);

            if (i_info.is_pcm) {
                if (!cavlc_consume_ipcm(br, sps, &event)) {
                    free(states);
                    return 0;
                }
                event.qp_y = curr_qp_y;
                avc_mb_fill_chroma_qp(&event, pps, sps);
                cavlc_mark_ipcm_nonzero(curr, sps);
                if (callbacks.on_macroblock) {
                    callbacks.on_macroblock(opaque, &event);
                }
                save_mb_state(states, max_mbs, &event, NULL);
                summary->macroblocks_seen++;
                mb_addr++;
                prev_mb_skipped = 0;
                summary->next_mb_address = mb_addr;
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
                if (avc_mb_chroma_array_type(sps) == 0) {
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
            curr_qp_y = avc_mb_qp_y_from_delta(curr_qp_y, event.mb_qp_delta, sps);
            event.qp_y = curr_qp_y;
            avc_mb_fill_chroma_qp(&event, pps, sps);
            if (callbacks.on_macroblock) {
                callbacks.on_macroblock(opaque, &event);
            }
            summary->macroblocks_seen++;
            if (!cavlc_emit_luma_residuals(br, mb_addr, pps, sps, &event,
                                           i_info.pred_kind,
                                           event.coded_block_pattern_luma,
                                           event.transform_size_8x8_flag,
                                           residual_scan_mode(slice, sps, &event),
                                           left, top, curr,
                                           callbacks, opaque) ||
                !cavlc_emit_chroma_residuals(br, mb_addr, pps, sps, &event,
                                             i_info.pred_kind,
                                             event.coded_block_pattern_chroma,
                                             residual_scan_mode(slice, sps, &event),
                                             left, top, curr,
                                             callbacks, opaque)) {
                free(states);
                return 0;
            }
            save_mb_state(states, max_mbs, &event, NULL);
        } else if (slice->slice_kind == AVC_SLICE_P || slice->slice_kind == AVC_SLICE_SP) {
            avc_p_mb_type_info_t p_info = avc_p_mb_type_classify(event.mb_type);
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
                        event.qp_y = curr_qp_y;
                        avc_mb_fill_chroma_qp(&event, pps, sps);
                        cavlc_mark_ipcm_nonzero(curr, sps);
                        if (callbacks.on_macroblock) {
                            callbacks.on_macroblock(opaque, &event);
                        }
                        save_mb_state(states, max_mbs, &event, NULL);
                        summary->macroblocks_seen++;
                        mb_addr++;
                        prev_mb_skipped = 0;
                        summary->next_mb_address = mb_addr;
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
                                                 left, top, top_right, top_left,
                                                 callbacks, opaque,
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
            if (avc_mb_chroma_array_type(sps) == 0) {
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
            curr_qp_y = avc_mb_qp_y_from_delta(curr_qp_y, event.mb_qp_delta, sps);
            event.qp_y = curr_qp_y;
            avc_mb_fill_chroma_qp(&event, pps, sps);
            if (callbacks.on_macroblock) {
                callbacks.on_macroblock(opaque, &event);
            }
            summary->macroblocks_seen++;
            if (!cavlc_emit_luma_residuals(br, mb_addr, pps, sps, &event,
                                           p_info.shape == AVC_P_MB_INTRA ? i_info.pred_kind : AVC_MB_PRED_INTER,
                                           event.coded_block_pattern_luma,
                                           event.transform_size_8x8_flag,
                                           residual_scan_mode(slice, sps, &event),
                                           left, top, curr,
                                           callbacks, opaque) ||
                !cavlc_emit_chroma_residuals(br, mb_addr, pps, sps, &event,
                                             p_info.shape == AVC_P_MB_INTRA ? i_info.pred_kind : AVC_MB_PRED_INTER,
                                             event.coded_block_pattern_chroma,
                                             residual_scan_mode(slice, sps, &event),
                                             left, top, curr,
                                             callbacks, opaque)) {
                free(states);
                return 0;
            }
            save_mb_state(states, max_mbs, &event, pred_for_state);
        } else if (slice->slice_kind == AVC_SLICE_B) {
            avc_b_mb_type_info_t b_info = avc_b_mb_type_classify(event.mb_type);
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
                    event.qp_y = curr_qp_y;
                    avc_mb_fill_chroma_qp(&event, pps, sps);
                    cavlc_mark_ipcm_nonzero(curr, sps);
                    if (callbacks.on_macroblock) {
                        callbacks.on_macroblock(opaque, &event);
                    }
                    save_mb_state(states, max_mbs, &event, NULL);
                    summary->macroblocks_seen++;
                    mb_addr++;
                    prev_mb_skipped = 0;
                    summary->next_mb_address = mb_addr;
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
                                                 left, top, top_right, top_left,
                                                 callbacks, opaque,
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
            if (avc_mb_chroma_array_type(sps) == 0) {
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
            curr_qp_y = avc_mb_qp_y_from_delta(curr_qp_y, event.mb_qp_delta, sps);
            event.qp_y = curr_qp_y;
            avc_mb_fill_chroma_qp(&event, pps, sps);
            if (callbacks.on_macroblock) {
                callbacks.on_macroblock(opaque, &event);
            }
            summary->macroblocks_seen++;
            if (!cavlc_emit_luma_residuals(br, mb_addr, pps, sps, &event,
                                           b_info.shape == AVC_B_MB_INTRA ? i_info.pred_kind : AVC_MB_PRED_INTER,
                                           event.coded_block_pattern_luma,
                                           event.transform_size_8x8_flag,
                                           residual_scan_mode(slice, sps, &event),
                                           left, top, curr,
                                           callbacks, opaque) ||
                !cavlc_emit_chroma_residuals(br, mb_addr, pps, sps, &event,
                                             b_info.shape == AVC_B_MB_INTRA ? i_info.pred_kind : AVC_MB_PRED_INTER,
                                             event.coded_block_pattern_chroma,
                                             residual_scan_mode(slice, sps, &event),
                                             left, top, curr,
                                             callbacks, opaque)) {
                free(states);
                return 0;
            }
            save_mb_state(states, max_mbs, &event, pred_for_state);
        }
        mb_addr++;
        prev_mb_skipped = 0;
        summary->next_mb_address = mb_addr;
    }

    summary->next_mb_address = mb_addr;
    summary->picture_complete = mb_addr >= max_mbs;
    summary->complete = !br->error;
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
    int32_t curr_qp_y;
    uint32_t mb_addr = slice->first_mb_in_slice;
    uint32_t max_mbs = avc_mb_pic_size_in_mbs(sps);
    uint32_t width = avc_mb_pic_width_in_mbs(sps);
    int prev_mb_qp_delta_nonzero = 0;
    int prev_mb_skipped = 0;
    avc_mb_state_t *states = (avc_mb_state_t *)calloc(max_mbs ? max_mbs : 1u, sizeof(*states));

    if (!states) {
        return 0;
    }

    while ((br->bit_pos & 7u) != 0) {
        avc_br_read_bit(br);
    }
    byte_pos = br->bit_pos >> 3;
    if (byte_pos >= br->size) {
        summary->next_mb_address = mb_addr;
        summary->picture_complete = mb_addr >= max_mbs;
        free(states);
        return !br->error;
    }

    curr_qp_y = 26 + pps->pic_init_qp_minus26 + slice->slice_qp_delta;
    slice_qp_y = (unsigned)curr_qp_y;
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
        const avc_mb_state_t *top_right;
        const avc_mb_state_t *top_left;
        avc_i_mb_type_info_t i_info;
        avc_p_mb_type_info_t p_info;
        avc_b_mb_type_info_t b_info;
        avc_mb_pred_event_t pred_event;
        avc_mb_pred_event_t *pred_for_state = NULL;
        int skipped = 0;

        summary->next_mb_address = mb_addr;
        event = (avc_macroblock_event_t){0};
        event.address = mb_addr;
        event.entropy = AVC_MB_ENTROPY_CABAC;
        event.qp_y = curr_qp_y;
        avc_mb_fill_chroma_qp(&event, pps, sps);
        i_info = (avc_i_mb_type_info_t){0};
        p_info = (avc_p_mb_type_info_t){0};
        b_info = (avc_b_mb_type_info_t){0};
        pred_event = (avc_mb_pred_event_t){0};
        neighbor_states4(states, max_mbs, width, mb_addr, &left, &top,
                         &top_right, &top_left);

        if (slice->slice_kind == AVC_SLICE_P || slice->slice_kind == AVC_SLICE_SP ||
            slice->slice_kind == AVC_SLICE_B) {
            skipped = avc_cabac_decode_mb_skip_flag(&cabac, slice->slice_kind,
                                                    left && left->available, left ? left->skipped : 0,
                                                    top && top->available, top ? top->skipped : 0);
            trace_mb(callbacks, opaque, mb_addr,
                     "CABAC trace mb=%u mb_skip_flag=%d bit=%zu range=%u offset=%u",
                     mb_addr, skipped, cabac.bit_pos, cabac.cod_i_range, cabac.cod_i_offset);
            debug_mb(callbacks, opaque, mb_addr,
                     "CABAC debug mb=%u after mb_skip_flag skipped=%d bit=%zu range=%u offset=%u",
                     mb_addr, skipped, cabac.bit_pos, cabac.cod_i_range, cabac.cod_i_offset);
            if (cabac.error) {
                summary->next_mb_address = mb_addr;
                notef(callbacks, opaque, "CABAC mb_skip_flag decode failed mb=%u bit=%zu",
                      mb_addr, cabac.bit_pos);
                free(states);
                return 0;
            }
            if (skipped) {
                avc_mb_pred_event_t skipped_pred;
                avc_mb_pred_event_t *skipped_pred_ptr = NULL;

                event.skipped = 1;
                event.mb_field_decoding_flag = mb_pair_field_flag(states, max_mbs, mb_addr);
                event.qp_y = curr_qp_y;
                avc_mb_fill_chroma_qp(&event, pps, sps);
                if (slice->slice_kind == AVC_SLICE_B) {
                    emit_b_direct_pred(mb_addr, slice, left, top, top_right, top_left,
                                       callbacks, opaque, &skipped_pred);
                    skipped_pred_ptr = &skipped_pred;
                } else {
                    emit_p_skip_pred(mb_addr, left, top, top_right, top_left,
                                     callbacks, opaque, &skipped_pred);
                    skipped_pred_ptr = &skipped_pred;
                }
                if (callbacks.on_macroblock) {
                    callbacks.on_macroblock(opaque, &event);
                }
                save_mb_state(states, max_mbs, &event, skipped_pred_ptr);
                summary->macroblocks_seen++;
                mb_addr++;
                summary->next_mb_address = mb_addr;
                prev_mb_qp_delta_nonzero = 0;
                prev_mb_skipped = 1;
                {
                    int end_of_slice = avc_cabac_decode_terminate(&cabac);
                    if (cabac.error) {
                        summary->next_mb_address = mb_addr;
                        notef(callbacks, opaque,
                              "CABAC end_of_slice_flag decode failed after skipped mb=%u bit=%zu",
                              mb_addr - 1u, cabac.bit_pos);
                        free(states);
                        return 0;
                    }
                    if (end_of_slice && !cabac_has_substantial_bits_left(&cabac)) {
                        summary->next_mb_address = mb_addr;
                        summary->picture_complete = mb_addr >= max_mbs;
                        summary->complete = 1;
                        free(states);
                        return !cabac.error;
                    }
                }
                continue;
            }
        }

        if (mb_field_flag_present(slice, sps, mb_addr, prev_mb_skipped)) {
            event.mb_field_decoding_flag =
                avc_cabac_decode_mb_field_decoding_flag(&cabac,
                                                        left && left->available,
                                                        left ? left->mb_field_decoding_flag : 0,
                                                        top && top->available,
                                                        top ? top->mb_field_decoding_flag : 0);
            debug_mb(callbacks, opaque, mb_addr,
                     "CABAC debug mb=%u after mb_field_decoding_flag=%d bit=%zu",
                     mb_addr, event.mb_field_decoding_flag, cabac.bit_pos);
            if (cabac.error) {
                notef(callbacks, opaque,
                      "CABAC mb_field_decoding_flag decode failed mb=%u bit=%zu",
                      mb_addr, cabac.bit_pos);
                free(states);
                return 0;
            }
        } else {
            event.mb_field_decoding_flag = mb_pair_field_flag(states, max_mbs, mb_addr);
        }
        prev_mb_skipped = 0;

        if (slice->slice_kind == AVC_SLICE_I || slice->slice_kind == AVC_SLICE_SI) {
            if (!avc_cabac_decode_mb_type_i(&cabac, &event.mb_type)) {
                notef(callbacks, opaque, "CABAC mb_type I decode failed mb=%u bit=%zu",
                      mb_addr, cabac.bit_pos);
                free(states);
                return 0;
            }
            i_info = avc_i_mb_type_classify(event.mb_type);
        } else if (slice->slice_kind == AVC_SLICE_B) {
            if (!avc_cabac_decode_mb_type_b(&cabac,
                                            left && left->available,
                                            state_is_b_direct(left),
                                            top && top->available,
                                            state_is_b_direct(top),
                                            &event.mb_type)) {
                notef(callbacks, opaque, "CABAC mb_type B decode failed mb=%u bit=%zu",
                      mb_addr, cabac.bit_pos);
                free(states);
                return 0;
            }
            debug_mb(callbacks, opaque, mb_addr,
                     "CABAC debug mb=%u after mb_type=%u bit=%zu range=%u offset=%u",
                     mb_addr, event.mb_type, cabac.bit_pos,
                     cabac.cod_i_range, cabac.cod_i_offset);
            b_info = avc_b_mb_type_classify(event.mb_type);
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
            trace_mb(callbacks, opaque, mb_addr,
                     "CABAC trace mb=%u enter P mb_type bit=%zu range=%u offset=%u",
                     mb_addr, cabac.bit_pos, cabac.cod_i_range, cabac.cod_i_offset);
            if (!avc_cabac_decode_mb_type_p(&cabac, &event.mb_type)) {
                notef(callbacks, opaque, "CABAC mb_type P decode failed mb=%u bit=%zu",
                      mb_addr, cabac.bit_pos);
                free(states);
                return 0;
            }
            debug_mb(callbacks, opaque, mb_addr,
                     "CABAC debug mb=%u after P mb_type=%u bit=%zu range=%u offset=%u",
                     mb_addr, event.mb_type, cabac.bit_pos,
                     cabac.cod_i_range, cabac.cod_i_offset);
            p_info = avc_p_mb_type_classify(event.mb_type);
            trace_mb(callbacks, opaque, mb_addr,
                     "CABAC trace mb=%u exit P mb_type value=%u shape=%s bit=%zu range=%u offset=%u",
                     mb_addr, event.mb_type, p_mb_shape_name(p_info.shape),
                     cabac.bit_pos, cabac.cod_i_range, cabac.cod_i_offset);
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
            unsigned pcm_bit_pos = 0;
            unsigned backup_bytes = 0;

            event.transform_size_8x8_flag = 0;
            if (!cabac_consume_ipcm(&cabac, sps, &event,
                                    &pcm_bit_pos,
                                    &backup_bytes)) {
                notef(callbacks, opaque,
                      "CABAC I_PCM payload decode failed mb=%u mb_type=%u bit=%zu pcm_bit_pos=%u cabac_backup_bytes=%u",
                      mb_addr, event.mb_type, cabac.bit_pos,
                      pcm_bit_pos, backup_bytes);
                free(states);
                return 0;
            }
            event.qp_y = curr_qp_y;
            avc_mb_fill_chroma_qp(&event, pps, sps);
            if (callbacks.on_macroblock) {
                callbacks.on_macroblock(opaque, &event);
            }
            save_mb_state(states, max_mbs, &event, NULL);
            summary->macroblocks_seen++;
            mb_addr++;
            summary->next_mb_address = mb_addr;
            prev_mb_qp_delta_nonzero = 0;
            prev_mb_skipped = 0;
            int end_of_slice = mb_addr >= max_mbs ? 1 : avc_cabac_decode_terminate(&cabac);
            if (mb_addr >= max_mbs || (end_of_slice && !cabac_has_substantial_bits_left(&cabac))) {
                if (cabac.error) {
                    notef(callbacks, opaque,
                          "CABAC end_of_slice_flag decode failed after I_PCM mb=%u bit=%zu",
                          mb_addr - 1u, cabac.bit_pos);
                    free(states);
                    return 0;
                }
                summary->next_mb_address = mb_addr;
                summary->picture_complete = mb_addr >= max_mbs;
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
            trace_mb(callbacks, opaque, mb_addr,
                     "CABAC trace mb=%u enter P mb_pred shape=%s partitions=%u num_ref_idx_l0_active_minus1=%u bit=%zu range=%u offset=%u",
                     mb_addr, p_mb_shape_name(p_info.shape),
                     avc_p_inter_partition_count(p_info.shape),
                     slice->num_ref_idx_l0_active_minus1,
                     cabac.bit_pos, cabac.cod_i_range, cabac.cod_i_offset);
            if (!cabac_parse_p_inter_mb_pred(&cabac, mb_addr, slice, p_info.shape,
                                             left, top, top_right, top_left,
                                             callbacks, opaque,
                                             &pred_event)) {
                notef(callbacks, opaque, "CABAC P inter mb_pred failed mb=%u mb_type=%u bit=%zu",
                      mb_addr, event.mb_type, cabac.bit_pos);
                free(states);
                return 0;
            }
            trace_mb(callbacks, opaque, mb_addr,
                     "CABAC trace mb=%u exit P mb_pred bit=%zu range=%u offset=%u",
                     mb_addr, cabac.bit_pos, cabac.cod_i_range, cabac.cod_i_offset);
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
                                             left, top, top_right, top_left,
                                             callbacks, opaque,
                                             &pred_event)) {
                notef(callbacks, opaque, "CABAC B inter mb_pred failed mb=%u mb_type=%u bit=%zu",
                      mb_addr, event.mb_type, cabac.bit_pos);
                free(states);
                return 0;
            }
            debug_mb(callbacks, opaque, mb_addr,
                     "CABAC debug mb=%u after B mb_pred bit=%zu range=%u offset=%u",
                     mb_addr, cabac.bit_pos, cabac.cod_i_range, cabac.cod_i_offset);
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
            debug_mb(callbacks, opaque, mb_addr,
                     "CABAC debug mb=%u after cbp_luma=%u bit=%zu range=%u offset=%u",
                     mb_addr, event.coded_block_pattern_luma, cabac.bit_pos,
                     cabac.cod_i_range, cabac.cod_i_offset);
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
            debug_mb(callbacks, opaque, mb_addr,
                     "CABAC debug mb=%u after cbp_chroma=%u bit=%zu range=%u offset=%u",
                     mb_addr, event.coded_block_pattern_chroma, cabac.bit_pos,
                     cabac.cod_i_range, cabac.cod_i_offset);
            if (pps->transform_8x8_mode_flag && event.coded_block_pattern_luma) {
                event.transform_size_8x8_flag =
                    avc_cabac_decode_transform_size_8x8_flag(&cabac,
                                                             left ? left->transform_size_8x8_flag : 0,
                                                             top ? top->transform_size_8x8_flag : 0);
                debug_mb(callbacks, opaque, mb_addr,
                         "CABAC debug mb=%u after transform_8x8=%d bit=%zu",
                         mb_addr, event.transform_size_8x8_flag, cabac.bit_pos);
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
                debug_mb(callbacks, opaque, mb_addr,
                         "CABAC debug mb=%u after mb_qp_delta=%d bit=%zu range=%u offset=%u",
                         mb_addr, event.mb_qp_delta, cabac.bit_pos,
                         cabac.cod_i_range, cabac.cod_i_offset);
            }
        }

        curr_qp_y = avc_mb_qp_y_from_delta(curr_qp_y, event.mb_qp_delta, sps);
        event.qp_y = curr_qp_y;
        avc_mb_fill_chroma_qp(&event, pps, sps);
        if (callbacks.on_macroblock) {
            callbacks.on_macroblock(opaque, &event);
        }
        save_mb_state(states, max_mbs, &event, pred_for_state);
        summary->macroblocks_seen++;

        if (i_info.pred_kind == AVC_MB_PRED_INTRA_4X4 ||
            i_info.pred_kind == AVC_MB_PRED_INTRA_8X8 ||
            i_info.pred_kind == AVC_MB_PRED_INTRA_16X16) {
            avc_mb_state_t *curr = state_for(states, max_mbs, mb_addr);
            if (!cabac_emit_luma_residuals(&cabac, mb_addr, pps, sps, &event,
                                           i_info.pred_kind,
                                           event.coded_block_pattern_luma,
                                           event.transform_size_8x8_flag,
                                           residual_scan_mode(slice, sps, &event),
                                           left, top, curr,
                                           callbacks, opaque)) {
                free(states);
                return 0;
            }
            if (!cabac_emit_chroma_residuals(&cabac, mb_addr, pps, sps, &event,
                                             i_info.pred_kind,
                                             event.coded_block_pattern_chroma,
                                             residual_scan_mode(slice, sps, &event),
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
            if (!cabac_emit_luma_residuals(&cabac, mb_addr, pps, sps, &event,
                                           AVC_MB_PRED_INTER,
                                           event.coded_block_pattern_luma,
                                           event.transform_size_8x8_flag,
                                           residual_scan_mode(slice, sps, &event),
                                           left, top, curr,
                                           callbacks, opaque)) {
                notef(callbacks, opaque,
                      "CABAC luma residual decode failed mb=%u bit=%zu",
                      mb_addr, cabac.bit_pos);
                free(states);
                return 0;
            }
            debug_mb(callbacks, opaque, mb_addr,
                     "CABAC debug mb=%u after luma residuals bit=%zu range=%u offset=%u",
                     mb_addr, cabac.bit_pos, cabac.cod_i_range, cabac.cod_i_offset);
            if (!cabac_emit_chroma_residuals(&cabac, mb_addr, pps, sps, &event,
                                             AVC_MB_PRED_INTER,
                                             event.coded_block_pattern_chroma,
                                             residual_scan_mode(slice, sps, &event),
                                             left, top, curr,
                                             callbacks, opaque)) {
                notef(callbacks, opaque,
                      "CABAC chroma residual decode failed mb=%u bit=%zu",
                      mb_addr, cabac.bit_pos);
                free(states);
                return 0;
            }
            debug_mb(callbacks, opaque, mb_addr,
                     "CABAC debug mb=%u after chroma residuals bit=%zu range=%u offset=%u",
                     mb_addr, cabac.bit_pos, cabac.cod_i_range, cabac.cod_i_offset);
        } else if (b_info.shape == AVC_B_MB_DIRECT ||
                   b_info.shape == AVC_B_MB_INTER ||
                   b_info.shape == AVC_B_MB_8X8) {
            avc_mb_state_t *curr = state_for(states, max_mbs, mb_addr);
            if (!cabac_emit_luma_residuals(&cabac, mb_addr, pps, sps, &event,
                                           AVC_MB_PRED_INTER,
                                           event.coded_block_pattern_luma,
                                           event.transform_size_8x8_flag,
                                           residual_scan_mode(slice, sps, &event),
                                           left, top, curr,
                                           callbacks, opaque)) {
                free(states);
                return 0;
            }
            if (!cabac_emit_chroma_residuals(&cabac, mb_addr, pps, sps, &event,
                                             AVC_MB_PRED_INTER,
                                             event.coded_block_pattern_chroma,
                                             residual_scan_mode(slice, sps, &event),
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
        summary->next_mb_address = mb_addr;
        prev_mb_qp_delta_nonzero = event.mb_qp_delta != 0;
        prev_mb_skipped = 0;
        if (mb_addr >= max_mbs) {
            summary->next_mb_address = mb_addr;
            summary->picture_complete = mb_addr >= max_mbs;
            summary->complete = 1;
            free(states);
            return 1;
        }
        {
            int end_of_slice = avc_cabac_decode_terminate(&cabac);
            if (cabac.error) {
                notef(callbacks, opaque,
                      "CABAC end_of_slice_flag decode failed after mb=%u bit=%zu",
                      mb_addr - 1u, cabac.bit_pos);
                free(states);
                return 0;
            }
            if (end_of_slice && !cabac_has_substantial_bits_left(&cabac)) {
                summary->next_mb_address = mb_addr;
                summary->picture_complete = 0;
                summary->complete = 1;
                free(states);
                return 1;
            }
        }
    }

    summary->next_mb_address = mb_addr;
    summary->picture_complete = mb_addr >= max_mbs;
    summary->complete = mb_addr >= max_mbs && !cabac.error;
    if (cabac.error) {
        notef(callbacks, opaque,
              "CABAC decoder stopped before mb=%u bit=%zu after parsed_mbs=%u",
              mb_addr, cabac.bit_pos, summary->macroblocks_seen);
    }
    free(states);
    return !cabac.error;
}

static int cabac_has_substantial_bits_left(const avc_cabac_decoder_t *cabac)
{
    size_t total_bits;

    if (!cabac || cabac->bit_pos >= cabac->size * 8u) {
        return 0;
    }
    total_bits = cabac->size * 8u;
    return total_bits - cabac->bit_pos > 8u;
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
    summary->first_mb_in_slice = slice->first_mb_in_slice;
    summary->next_mb_address = slice->first_mb_in_slice;
    summary->max_macroblocks = avc_mb_pic_size_in_mbs(sps);

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
