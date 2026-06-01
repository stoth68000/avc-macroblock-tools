#include "avc_macroblock_layout.h"

uint32_t avc_mb_pic_size_in_mbs(const avc_sps_t *sps)
{
    uint32_t width = sps->pic_width_in_mbs_minus1 + 1u;
    uint32_t height = (sps->pic_height_in_map_units_minus1 + 1u) *
                      (2u - sps->frame_mbs_only_flag);
    return width * height;
}

uint32_t avc_mb_pic_width_in_mbs(const avc_sps_t *sps)
{
    return sps->pic_width_in_mbs_minus1 + 1u;
}

unsigned avc_mb_chroma_array_type(const avc_sps_t *sps)
{
    if (sps->separate_colour_plane_flag) {
        return 0;
    }
    return sps->chroma_format_idc;
}

unsigned avc_mb_chroma_dc_coeffs(unsigned chroma_format_idc)
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

unsigned avc_mb_chroma_ac_blocks_per_component(unsigned chroma_format_idc)
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

unsigned avc_mb_chroma_ac_width(unsigned chroma_format)
{
    return chroma_format == 3 ? 4u : 2u;
}

int avc_mb_luma4x4_block_origin(unsigned block_index, unsigned *x, unsigned *y)
{
    unsigned group = block_index / 4u;
    unsigned sub = block_index & 3u;

    if (block_index >= 16) {
        return 0;
    }
    *x = ((group & 1u) * 8u) + ((sub & 1u) * 4u);
    *y = ((group >> 1) * 8u) + ((sub >> 1) * 4u);
    return 1;
}

int avc_mb_luma8x8_block_origin(unsigned block_index, unsigned *x, unsigned *y)
{
    if (block_index >= 4) {
        return 0;
    }
    *x = (block_index & 1u) * 8u;
    *y = (block_index >> 1) * 8u;
    return 1;
}

int avc_mb_chroma4x4_block_origin(unsigned chroma_format,
                                  unsigned component_block_index,
                                  unsigned *x,
                                  unsigned *y)
{
    unsigned blocks_wide;

    if (chroma_format == 1) {
        if (component_block_index >= 4) {
            return 0;
        }
        blocks_wide = 2;
    } else if (chroma_format == 2) {
        if (component_block_index >= 8) {
            return 0;
        }
        blocks_wide = 2;
    } else if (chroma_format == 3) {
        if (component_block_index >= 16) {
            return 0;
        }
        return avc_mb_luma4x4_block_origin(component_block_index, x, y);
    } else {
        return 0;
    }

    *x = (component_block_index % blocks_wide) * 4u;
    *y = (component_block_index / blocks_wide) * 4u;
    return 1;
}

static int32_t clip_int32(int32_t value, int32_t min_value, int32_t max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

int32_t avc_mb_qp_y_from_delta(int32_t previous_qp_y,
                               int32_t mb_qp_delta,
                               const avc_sps_t *sps)
{
    int32_t qp_bd_offset_y = 6 * (int32_t)sps->bit_depth_luma_minus8;
    int32_t qp_range = 52 + qp_bd_offset_y;
    int32_t qp_y = previous_qp_y + mb_qp_delta + 52 + 2 * qp_bd_offset_y;

    qp_y %= qp_range;
    if (qp_y < 0) {
        qp_y += qp_range;
    }
    return qp_y - qp_bd_offset_y;
}

static int32_t qp_chroma_from_luma(int32_t qp_y,
                                   int32_t chroma_qp_index_offset,
                                   const avc_sps_t *sps)
{
    static const int8_t qp_chroma_map[52] = {
        0, 1, 2, 3, 4, 5, 6, 7,
        8, 9, 10, 11, 12, 13, 14, 15,
        16, 17, 18, 19, 20, 21, 22, 23,
        24, 25, 26, 27, 28, 29, 29, 30,
        31, 32, 32, 33, 34, 34, 35, 35,
        36, 36, 37, 37, 37, 38, 38, 38,
        39, 39, 39, 39
    };
    int32_t qp_bd_offset_c = 6 * (int32_t)sps->bit_depth_chroma_minus8;
    int32_t qp_i = clip_int32(qp_y + chroma_qp_index_offset, -qp_bd_offset_c, 51);

    if (qp_i < 0) {
        return qp_i;
    }
    return (int32_t)qp_chroma_map[qp_i] + qp_bd_offset_c;
}

void avc_mb_fill_chroma_qp(avc_macroblock_event_t *event,
                           const avc_pps_t *pps,
                           const avc_sps_t *sps)
{
    if (avc_mb_chroma_array_type(sps) == 0) {
        event->qp_cb = -1;
        event->qp_cr = -1;
        return;
    }
    event->qp_cb = qp_chroma_from_luma(event->qp_y, pps->chroma_qp_index_offset, sps);
    event->qp_cr = qp_chroma_from_luma(event->qp_y, pps->second_chroma_qp_index_offset, sps);
}
