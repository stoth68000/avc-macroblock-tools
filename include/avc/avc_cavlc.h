#ifndef AVC_CAVLC_H
#define AVC_CAVLC_H

#include <stddef.h>
#include <stdint.h>
#include "avc/avc_bitreader.h"

#define AVC_CAVLC_MAX_COEFFS 64

typedef struct {
    unsigned total_coeff;
    unsigned trailing_ones;
    int coeff_level[AVC_CAVLC_MAX_COEFFS];
    unsigned total_zeros;
    unsigned run_before[AVC_CAVLC_MAX_COEFFS];
} avc_cavlc_block_t;

typedef struct {
    void (*on_coeff)(void *opaque, unsigned scan_index, int level, unsigned run_before);
} avc_cavlc_callbacks_t;

int avc_cavlc_read_residual_block(avc_bitreader_t *br, int nC, unsigned max_coeff,
                                  avc_cavlc_block_t *block,
                                  avc_cavlc_callbacks_t callbacks,
                                  void *opaque);

#endif
