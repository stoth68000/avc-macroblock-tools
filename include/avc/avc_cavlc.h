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
    unsigned coeff_scan[AVC_CAVLC_MAX_COEFFS];
    unsigned coeff_x[AVC_CAVLC_MAX_COEFFS];
    unsigned coeff_y[AVC_CAVLC_MAX_COEFFS];
    unsigned total_zeros;
    unsigned run_before[AVC_CAVLC_MAX_COEFFS];
} avc_cavlc_block_t;

typedef enum {
    AVC_CAVLC_SCAN_FRAME = 0,
    AVC_CAVLC_SCAN_FIELD = 1,
    AVC_CAVLC_SCAN_TRANSFORM_BYPASS_FRAME = 2,
    AVC_CAVLC_SCAN_TRANSFORM_BYPASS_FIELD = 3
} avc_cavlc_scan_t;

typedef struct {
    void (*on_coeff)(void *opaque, unsigned scan_index, int level, unsigned run_before);
} avc_cavlc_callbacks_t;

int avc_cavlc_scan_position(unsigned max_coeff,
                            unsigned scan,
                            avc_cavlc_scan_t scan_mode,
                            unsigned *x,
                            unsigned *y);
int avc_cavlc_read_level(avc_bitreader_t *br, unsigned suffix_length, int *level);
int avc_cavlc_read_coeff_token(avc_bitreader_t *br, int nC, unsigned max_coeff,
                               unsigned *total_coeff, unsigned *trailing_ones);
int avc_cavlc_read_total_zeros(avc_bitreader_t *br, unsigned max_coeff,
                               unsigned total_coeff, unsigned *total_zeros);
int avc_cavlc_read_run_before(avc_bitreader_t *br, unsigned zeros_left,
                              unsigned *run_before);
int avc_cavlc_derive_nC_from_neighbors(int left_available, unsigned left_count,
                                       int top_available, unsigned top_count);
int avc_cavlc_read_residual_block(avc_bitreader_t *br, int nC, unsigned max_coeff,
                                  avc_cavlc_scan_t scan_mode,
                                  avc_cavlc_block_t *block,
                                  avc_cavlc_callbacks_t callbacks,
                                  void *opaque);

#endif
