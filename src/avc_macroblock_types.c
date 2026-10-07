#include "avc_macroblock_types.h"

int avc_mb_neighbor_addresses(uint32_t pic_size_in_mbs,
                              uint32_t pic_width_in_mbs,
                              uint32_t mb_address,
                              avc_mb_neighbor_addresses_t *neighbors)
{
    uint32_t col;

    if (!neighbors) {
        return 0;
    }
    *neighbors = (avc_mb_neighbor_addresses_t){0};
    if (pic_width_in_mbs == 0 || mb_address >= pic_size_in_mbs) {
        return 0;
    }

    col = mb_address % pic_width_in_mbs;
    if (mb_address > 0 && col != 0) {
        neighbors->left_available = 1;
        neighbors->left = mb_address - 1u;
    }
    if (mb_address >= pic_width_in_mbs) {
        neighbors->top_available = 1;
        neighbors->top = mb_address - pic_width_in_mbs;
        if (col + 1u < pic_width_in_mbs &&
            mb_address - pic_width_in_mbs + 1u < pic_size_in_mbs) {
            neighbors->top_right_available = 1;
            neighbors->top_right = mb_address - pic_width_in_mbs + 1u;
        }
        if (col > 0) {
            neighbors->top_left_available = 1;
            neighbors->top_left = mb_address - pic_width_in_mbs - 1u;
        }
    }
    return 1;
}

int avc_mb_pred_partition_at(const avc_mb_pred_event_t *pred,
                             unsigned x,
                             unsigned y,
                             unsigned *partition)
{
    unsigned i;

    if (!partition || !pred || pred->kind != AVC_MB_PRED_INTER ||
        pred->partition_count == 0 || x >= 16 || y >= 16) {
        return 0;
    }
    for (i = 0; i < pred->partition_count && i < AVC_MB_PRED_MAX_PARTITIONS; i++) {
        unsigned px = pred->partition_x[i];
        unsigned py = pred->partition_y[i];
        unsigned pw = pred->partition_width[i];
        unsigned ph = pred->partition_height[i];

        if (pw == 0 || ph == 0) {
            continue;
        }
        if (x >= px && x < px + pw && y >= py && y < py + ph) {
            *partition = i;
            return 1;
        }
    }
    if (pred->partition_count == 1) {
        *partition = 0;
        return 1;
    }
    return 0;
}

int avc_mb_pred_sub_partition_position(const avc_mb_pred_event_t *pred,
                                       unsigned partition,
                                       unsigned sub_partition,
                                       unsigned *x,
                                       unsigned *y,
                                       unsigned *width,
                                       unsigned *height)
{
    unsigned sub_width;
    unsigned sub_height;
    unsigned col_count;
    unsigned row_count;
    unsigned sub_count;

    if (!pred || !x || !y || !width || !height ||
        partition >= pred->partition_count ||
        partition >= AVC_MB_PRED_MAX_PARTITIONS) {
        return 0;
    }

    sub_width = pred->sub_partition_width[partition];
    sub_height = pred->sub_partition_height[partition];
    if (sub_width == 0 || sub_height == 0) {
        sub_width = pred->partition_width[partition];
        sub_height = pred->partition_height[partition];
    }
    if (sub_width == 0 || sub_height == 0 ||
        pred->partition_width[partition] == 0 ||
        pred->partition_height[partition] == 0) {
        return 0;
    }

    col_count = pred->partition_width[partition] / sub_width;
    row_count = pred->partition_height[partition] / sub_height;
    if (col_count == 0 || row_count == 0) {
        return 0;
    }
    sub_count = pred->sub_partition_count[partition] ?
        pred->sub_partition_count[partition] : col_count * row_count;
    if (sub_partition >= sub_count || sub_partition >= col_count * row_count) {
        return 0;
    }

    *x = pred->partition_x[partition] + (sub_partition % col_count) * sub_width;
    *y = pred->partition_y[partition] + (sub_partition / col_count) * sub_height;
    *width = sub_width;
    *height = sub_height;
    return 1;
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

void avc_mb_predict_mv(unsigned partition,
                       unsigned partition_width,
                       unsigned partition_height,
                       avc_mv_predictor_candidate_t a,
                       avc_mv_predictor_candidate_t b,
                       avc_mv_predictor_candidate_t c,
                       int16_t mv_pred[2])
{
    if (!mv_pred) {
        return;
    }

    if (!a.available) {
        a.mv[0] = 0;
        a.mv[1] = 0;
    }
    if (!b.available) {
        b.mv[0] = 0;
        b.mv[1] = 0;
    }
    if (!c.available) {
        c.mv[0] = 0;
        c.mv[1] = 0;
    }

    if (partition_width == 16 && partition_height == 8 && partition == 0 && b.available) {
        mv_pred[0] = b.mv[0];
        mv_pred[1] = b.mv[1];
    } else if (partition_width == 16 && partition_height == 8 && partition == 1 && a.available) {
        mv_pred[0] = a.mv[0];
        mv_pred[1] = a.mv[1];
    } else if (partition_width == 8 && partition_height == 16 && partition == 0 && a.available) {
        mv_pred[0] = a.mv[0];
        mv_pred[1] = a.mv[1];
    } else if (partition_width == 8 && partition_height == 16 && partition == 1 && c.available) {
        mv_pred[0] = c.mv[0];
        mv_pred[1] = c.mv[1];
    } else if (a.available && !b.available && !c.available) {
        mv_pred[0] = a.mv[0];
        mv_pred[1] = a.mv[1];
    } else if (!a.available && b.available && !c.available) {
        mv_pred[0] = b.mv[0];
        mv_pred[1] = b.mv[1];
    } else if (!a.available && !b.available && c.available) {
        mv_pred[0] = c.mv[0];
        mv_pred[1] = c.mv[1];
    } else {
        mv_pred[0] = median_i16(a.mv[0], b.mv[0], c.mv[0]);
        mv_pred[1] = median_i16(a.mv[1], b.mv[1], c.mv[1]);
    }
}

void avc_mb_derive_p_skip_mv(avc_mv_predictor_candidate_t a,
                             avc_mv_predictor_candidate_t b,
                             avc_mv_predictor_candidate_t c,
                             int16_t mv[2])
{
    if (!mv) {
        return;
    }

    mv[0] = 0;
    mv[1] = 0;
    if (!a.available || !b.available ||
        (a.mv[0] == 0 && a.mv[1] == 0) ||
        (b.mv[0] == 0 && b.mv[1] == 0)) {
        return;
    }

    avc_mb_predict_mv(0, 16, 16, a, b, c, mv);
}

int avc_mb_direct_spatial_ref_idx(avc_direct_spatial_candidate_t a,
                                  avc_direct_spatial_candidate_t b,
                                  avc_direct_spatial_candidate_t c,
                                  unsigned list_bit,
                                  unsigned *ref_idx)
{
    avc_direct_spatial_candidate_t candidates[3];
    unsigned i;
    unsigned best = 0;
    int have = 0;

    if (!ref_idx || (list_bit != 1u && list_bit != 2u)) {
        return 0;
    }
    *ref_idx = 0;
    candidates[0] = a;
    candidates[1] = b;
    candidates[2] = c;
    for (i = 0; i < 3; i++) {
        unsigned candidate_ref;
        if (!candidates[i].available || (candidates[i].list_mask & list_bit) == 0) {
            continue;
        }
        candidate_ref = list_bit == 1u ? candidates[i].ref_idx_l0 :
                                         candidates[i].ref_idx_l1;
        if (!have || candidate_ref < best) {
            best = candidate_ref;
            have = 1;
        }
    }
    if (have) {
        *ref_idx = best;
    }
    return have;
}

avc_i_mb_type_info_t avc_i_mb_type_classify(uint32_t mb_type)
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

avc_p_mb_type_info_t avc_p_mb_type_classify(uint32_t mb_type)
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
        info.intra = avc_i_mb_type_classify(mb_type - 5u);
    }
    return info;
}

avc_b_mb_type_info_t avc_b_mb_type_classify(uint32_t mb_type)
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
        info.partition_width[0] = 16;
        info.partition_height[0] = 16;
    } else if (mb_type >= 1 && mb_type <= 21) {
        info.shape = AVC_B_MB_INTER;
        info.partition_count = mb_type <= 3 ? 1u : 2u;
        info.list_mask[0] = masks[mb_type][0];
        info.list_mask[1] = masks[mb_type][1];
        if (mb_type <= 3) {
            info.partition_width[0] = 16;
            info.partition_height[0] = 16;
        } else if ((mb_type & 1u) == 0) {
            info.partition_width[0] = 16;
            info.partition_height[0] = 8;
            info.partition_width[1] = 16;
            info.partition_height[1] = 8;
        } else {
            info.partition_width[0] = 8;
            info.partition_height[0] = 16;
            info.partition_width[1] = 8;
            info.partition_height[1] = 16;
        }
    } else if (mb_type == 22) {
        info.shape = AVC_B_MB_8X8;
        info.partition_count = 4;
    } else if (mb_type >= 23 && mb_type <= 48) {
        info.shape = AVC_B_MB_INTRA;
        info.intra = avc_i_mb_type_classify(mb_type - 23u);
    }
    return info;
}

unsigned avc_p_inter_partition_count(avc_p_mb_shape_t shape)
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

void avc_mb_set_partition_geometry(avc_mb_pred_event_t *pred,
                                   unsigned partition,
                                   unsigned x,
                                   unsigned y,
                                   unsigned width,
                                   unsigned height,
                                   unsigned sub_count,
                                   unsigned sub_width,
                                   unsigned sub_height)
{
    if (!pred || partition >= 4) {
        return;
    }
    pred->partition_x[partition] = (uint8_t)x;
    pred->partition_y[partition] = (uint8_t)y;
    pred->partition_width[partition] = (uint8_t)width;
    pred->partition_height[partition] = (uint8_t)height;
    pred->sub_partition_count[partition] = (uint8_t)sub_count;
    pred->sub_partition_width[partition] = (uint8_t)sub_width;
    pred->sub_partition_height[partition] = (uint8_t)sub_height;
}

static void p_sub_partition_shape(unsigned sub_mb_type,
                                  unsigned *sub_count,
                                  unsigned *sub_width,
                                  unsigned *sub_height)
{
    if (sub_mb_type == 1) {
        *sub_count = 2;
        *sub_width = 8;
        *sub_height = 4;
    } else if (sub_mb_type == 2) {
        *sub_count = 2;
        *sub_width = 4;
        *sub_height = 8;
    } else if (sub_mb_type == 3) {
        *sub_count = 4;
        *sub_width = 4;
        *sub_height = 4;
    } else {
        *sub_count = 1;
        *sub_width = 8;
        *sub_height = 8;
    }
}

static void b_sub_partition_shape(unsigned sub_mb_type,
                                  unsigned *sub_count,
                                  unsigned *sub_width,
                                  unsigned *sub_height)
{
    if (sub_mb_type == 4 || sub_mb_type == 6 || sub_mb_type == 8) {
        *sub_count = 2;
        *sub_width = 8;
        *sub_height = 4;
    } else if (sub_mb_type == 5 || sub_mb_type == 7 || sub_mb_type == 9) {
        *sub_count = 2;
        *sub_width = 4;
        *sub_height = 8;
    } else if (sub_mb_type == 10 || sub_mb_type == 11 || sub_mb_type == 12) {
        *sub_count = 4;
        *sub_width = 4;
        *sub_height = 4;
    } else {
        *sub_count = 1;
        *sub_width = 8;
        *sub_height = 8;
    }
}

void avc_p_mb_set_partition_geometry(avc_mb_pred_event_t *pred,
                                     avc_p_mb_shape_t shape)
{
    unsigned i;

    if (!pred) {
        return;
    }
    if (shape == AVC_P_MB_L0_16X16) {
        avc_mb_set_partition_geometry(pred, 0, 0, 0, 16, 16, 1, 16, 16);
    } else if (shape == AVC_P_MB_L0_L0_16X8) {
        avc_mb_set_partition_geometry(pred, 0, 0, 0, 16, 8, 1, 16, 8);
        avc_mb_set_partition_geometry(pred, 1, 0, 8, 16, 8, 1, 16, 8);
    } else if (shape == AVC_P_MB_L0_L0_8X16) {
        avc_mb_set_partition_geometry(pred, 0, 0, 0, 8, 16, 1, 8, 16);
        avc_mb_set_partition_geometry(pred, 1, 8, 0, 8, 16, 1, 8, 16);
    } else if (shape == AVC_P_MB_8X8 || shape == AVC_P_MB_8X8REF0) {
        for (i = 0; i < 4; i++) {
            unsigned sub_count;
            unsigned sub_width;
            unsigned sub_height;
            p_sub_partition_shape(pred->sub_mb_type[i], &sub_count,
                                  &sub_width, &sub_height);
            avc_mb_set_partition_geometry(pred, i, (i & 1u) * 8u, (i >> 1) * 8u,
                                          8, 8, sub_count, sub_width, sub_height);
        }
    }
}

void avc_b_mb_set_partition_geometry(avc_mb_pred_event_t *pred,
                                     const avc_b_mb_type_info_t *info)
{
    unsigned i;

    if (!pred || !info) {
        return;
    }
    if (info->shape == AVC_B_MB_8X8) {
        for (i = 0; i < 4; i++) {
            unsigned sub_count;
            unsigned sub_width;
            unsigned sub_height;
            b_sub_partition_shape(pred->sub_mb_type[i], &sub_count,
                                  &sub_width, &sub_height);
            avc_mb_set_partition_geometry(pred, i, (i & 1u) * 8u, (i >> 1) * 8u,
                                          8, 8, sub_count, sub_width, sub_height);
        }
        return;
    }
    for (i = 0; i < info->partition_count && i < 4; i++) {
        unsigned width = info->partition_width[i] ? info->partition_width[i] : 16u;
        unsigned height = info->partition_height[i] ? info->partition_height[i] : 16u;
        unsigned x = width == 8 ? i * 8u : 0u;
        unsigned y = height == 8 ? i * 8u : 0u;
        avc_mb_set_partition_geometry(pred, i, x, y, width, height, 1, width, height);
    }
}

static void copy_mv_pair(int16_t dst[2], const int16_t src[2])
{
    dst[0] = src[0];
    dst[1] = src[1];
}

void avc_mb_populate_sub_partition_motion(avc_mb_pred_event_t *pred)
{
    unsigned partition;
    unsigned out = 0;

    if (!pred) {
        return;
    }
    pred->sub_partition_total = 0;
    for (partition = 0;
         partition < pred->partition_count && partition < AVC_MB_PRED_MAX_PARTITIONS;
         partition++) {
        unsigned sub_count = pred->sub_partition_count[partition];
        unsigned sub_width = pred->sub_partition_width[partition];
        unsigned sub_height = pred->sub_partition_height[partition];
        unsigned col_count;
        unsigned sub;

        if (sub_count == 0) {
            sub_count = 1;
            sub_width = pred->partition_width[partition];
            sub_height = pred->partition_height[partition];
        }
        col_count = sub_width ? pred->partition_width[partition] / sub_width : 1u;
        if (col_count == 0) {
            col_count = 1;
        }
        for (sub = 0; sub < sub_count && out < AVC_MB_PRED_MAX_SUB_PARTITIONS; sub++) {
            pred->sub_partition_parent[out] = (uint8_t)partition;
            pred->sub_part_x[out] =
                (uint8_t)(pred->partition_x[partition] + (sub % col_count) * sub_width);
            pred->sub_part_y[out] =
                (uint8_t)(pred->partition_y[partition] + (sub / col_count) * sub_height);
            pred->sub_part_width[out] = (uint8_t)sub_width;
            pred->sub_part_height[out] = (uint8_t)sub_height;
            pred->sub_part_list_mask[out] = pred->list_mask[partition];
            pred->sub_part_direct_flag[out] = pred->direct_flag[partition];
            pred->sub_part_ref_idx_l0[out] = pred->ref_idx_l0[partition];
            pred->sub_part_ref_idx_l1[out] = pred->ref_idx_l1[partition];
            copy_mv_pair(pred->sub_part_mvd_l0[out], pred->mvd_l0[partition]);
            copy_mv_pair(pred->sub_part_mvd_l1[out], pred->mvd_l1[partition]);
            copy_mv_pair(pred->sub_part_mv_pred_l0[out], pred->mv_pred_l0[partition]);
            copy_mv_pair(pred->sub_part_mv_pred_l1[out], pred->mv_pred_l1[partition]);
            copy_mv_pair(pred->sub_part_mv_l0[out], pred->mv_l0[partition]);
            copy_mv_pair(pred->sub_part_mv_l1[out], pred->mv_l1[partition]);
            out++;
        }
    }
    pred->sub_partition_total = out;
}

uint8_t avc_b_sub_mb_list_mask(unsigned sub_mb_type)
{
    switch (sub_mb_type) {
    case 0: return 0;
    case 1: case 4: case 5: case 10: return 1;
    case 2: case 6: case 7: case 11: return 2;
    case 3: case 8: case 9: case 12: return 3;
    default: return 0;
    }
}

int avc_b_sub_mb_is_direct(unsigned sub_mb_type)
{
    return sub_mb_type == 0;
}

void avc_p_mb_partition_neighbors(avc_p_mb_shape_t shape, unsigned partition,
                                  int *left_current, unsigned *left_partition,
                                  int *top_current, unsigned *top_partition,
                                  int *top_right_current,
                                  unsigned *top_right_partition)
{
    *left_current = 0;
    *top_current = 0;
    *top_right_current = 0;
    *left_partition = partition;
    *top_partition = partition;
    *top_right_partition = partition;

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
            *top_right_current = 1;
            *top_right_partition = 3;
        } else if (partition == 3) {
            *left_current = 1;
            *left_partition = 2;
            *top_current = 1;
            *top_partition = 1;
        } else {
            *top_right_current = 1;
            *top_right_partition = 1;
        }
    }
}
