#ifndef AVC_BITREADER_H
#define AVC_BITREADER_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t bit_pos;
    int error;
} avc_bitreader_t;

void avc_br_init(avc_bitreader_t *br, const uint8_t *data, size_t size);
size_t avc_br_bits_left(const avc_bitreader_t *br);
uint32_t avc_br_read_bits(avc_bitreader_t *br, unsigned n);
uint32_t avc_br_read_bit(avc_bitreader_t *br);
uint32_t avc_br_read_ue(avc_bitreader_t *br);
int32_t avc_br_read_se(avc_bitreader_t *br);
int avc_br_more_rbsp_data(const avc_bitreader_t *br);
int avc_br_consume_rbsp_trailing_bits(avc_bitreader_t *br);

#endif
