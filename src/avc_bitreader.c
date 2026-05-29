#include "avc/avc_bitreader.h"

void avc_br_init(avc_bitreader_t *br, const uint8_t *data, size_t size)
{
    br->data = data;
    br->size = size;
    br->bit_pos = 0;
    br->error = 0;
}

size_t avc_br_bits_left(const avc_bitreader_t *br)
{
    size_t total = br->size * 8;
    return br->bit_pos <= total ? total - br->bit_pos : 0;
}

uint32_t avc_br_read_bits(avc_bitreader_t *br, unsigned n)
{
    uint32_t value = 0;
    unsigned i;

    if (n > 32 || avc_br_bits_left(br) < n) {
        br->error = 1;
        return 0;
    }

    for (i = 0; i < n; i++) {
        size_t byte_pos = br->bit_pos >> 3;
        unsigned bit_in_byte = 7u - (unsigned)(br->bit_pos & 7u);
        value = (value << 1) | ((br->data[byte_pos] >> bit_in_byte) & 1u);
        br->bit_pos++;
    }
    return value;
}

uint32_t avc_br_read_bit(avc_bitreader_t *br)
{
    return avc_br_read_bits(br, 1);
}

uint32_t avc_br_read_ue(avc_bitreader_t *br)
{
    unsigned leading_zero_bits = 0;
    uint32_t suffix = 0;

    while (avc_br_bits_left(br) > 0 && avc_br_read_bit(br) == 0) {
        leading_zero_bits++;
        if (leading_zero_bits == 32) {
            br->error = 1;
            return 0;
        }
    }

    if (br->error) {
        return 0;
    }

    if (leading_zero_bits > 0) {
        suffix = avc_br_read_bits(br, leading_zero_bits);
    }
    return ((1u << leading_zero_bits) - 1u) + suffix;
}

int32_t avc_br_read_se(avc_bitreader_t *br)
{
    uint32_t code_num = avc_br_read_ue(br);
    int32_t value = (int32_t)((code_num + 1u) >> 1);
    return (code_num & 1u) ? value : -value;
}

int avc_br_more_rbsp_data(const avc_bitreader_t *br)
{
    avc_bitreader_t tmp = *br;
    size_t bits_left = avc_br_bits_left(&tmp);
    size_t i;

    if (bits_left == 0) {
        return 0;
    }
    if (avc_br_read_bit(&tmp) != 1) {
        return 1;
    }
    for (i = 1; i < bits_left; i++) {
        if (avc_br_read_bit(&tmp) != 0) {
            return 1;
        }
    }
    return 0;
}

int avc_br_consume_rbsp_trailing_bits(avc_bitreader_t *br)
{
    if (avc_br_bits_left(br) == 0 || avc_br_read_bit(br) != 1) {
        br->error = 1;
        return 0;
    }
    while ((br->bit_pos & 7u) != 0) {
        if (avc_br_read_bit(br) != 0) {
            br->error = 1;
            return 0;
        }
    }
    return !br->error;
}
