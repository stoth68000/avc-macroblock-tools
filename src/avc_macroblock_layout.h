#ifndef AVC_MACROBLOCK_LAYOUT_H
#define AVC_MACROBLOCK_LAYOUT_H

#include <stdint.h>
#include "avc/avc_macroblock.h"
#include "avc/avc_syntax.h"

uint32_t avc_mb_pic_size_in_mbs(const avc_sps_t *sps);
uint32_t avc_mb_pic_width_in_mbs(const avc_sps_t *sps);

unsigned avc_mb_chroma_array_type(const avc_sps_t *sps);
unsigned avc_mb_chroma_dc_coeffs(unsigned chroma_format_idc);
unsigned avc_mb_chroma_ac_blocks_per_component(unsigned chroma_format_idc);
unsigned avc_mb_chroma_ac_width(unsigned chroma_format);

int avc_mb_luma4x4_block_origin(unsigned block_index, unsigned *x, unsigned *y);
int avc_mb_luma8x8_block_origin(unsigned block_index, unsigned *x, unsigned *y);
int avc_mb_chroma4x4_block_origin(unsigned chroma_format,
                                  unsigned component_block_index,
                                  unsigned *x,
                                  unsigned *y);

int32_t avc_mb_qp_y_from_delta(int32_t previous_qp_y,
                               int32_t mb_qp_delta,
                               const avc_sps_t *sps);
void avc_mb_fill_chroma_qp(avc_macroblock_event_t *event,
                           const avc_pps_t *pps,
                           const avc_sps_t *sps);

#endif
