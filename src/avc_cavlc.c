#include "avc/avc_cavlc.h"
#include <string.h>

static const uint8_t coeff_token_len[4][4 * 17] = {
    {
        1,0,0,0, 6,2,0,0, 8,6,3,0, 9,8,7,5,
        10,9,8,6, 11,10,9,7, 13,11,10,8, 13,13,11,9,
        13,13,13,10, 14,14,13,11, 14,14,14,13, 15,15,14,14,
        15,15,15,14, 16,15,15,15, 16,16,16,15, 16,16,16,16,
        16,16,16,16
    },
    {
        2,0,0,0, 6,2,0,0, 6,5,3,0, 7,6,6,4,
        8,6,6,4, 8,7,7,5, 9,8,8,6, 11,9,9,6,
        11,11,11,7, 12,11,11,9, 12,12,12,11, 12,12,12,11,
        13,13,13,12, 13,13,13,13, 13,14,13,13, 14,14,14,13,
        14,14,14,14
    },
    {
        4,0,0,0, 6,4,0,0, 6,5,4,0, 6,5,5,4,
        7,5,5,4, 7,5,5,4, 7,6,6,4, 7,6,6,4,
        8,7,7,5, 8,8,7,6, 9,8,8,7, 9,9,8,8,
        9,9,9,8, 10,9,9,9, 10,10,10,10, 10,10,10,10,
        10,10,10,10
    },
    {
        6,0,0,0, 6,6,0,0, 6,6,6,0, 6,6,6,6,
        6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6,
        6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6,
        6,6,6,6, 6,6,6,6, 6,6,6,6, 6,6,6,6,
        6,6,6,6
    }
};

static const uint8_t coeff_token_bits[4][4 * 17] = {
    {
        1,0,0,0, 5,1,0,0, 7,4,1,0, 7,6,5,3,
        7,6,5,3, 7,6,5,4, 15,6,5,4, 11,14,5,4,
        8,10,13,4, 15,14,9,4, 11,10,13,12, 15,14,9,12,
        11,10,13,8, 15,1,9,12, 11,14,13,8, 7,10,9,12,
        4,6,5,8
    },
    {
        3,0,0,0, 11,2,0,0, 7,7,3,0, 7,10,9,5,
        7,6,5,4, 4,6,5,6, 7,6,5,8, 15,6,5,4,
        11,14,13,4, 15,10,9,4, 11,14,13,12, 8,10,9,8,
        15,14,13,12, 11,10,9,12, 7,11,6,8, 9,8,10,1,
        7,6,5,4
    },
    {
        15,0,0,0, 15,14,0,0, 11,15,13,0, 8,12,14,12,
        15,10,11,11, 11,8,9,10, 9,14,13,9, 8,10,9,8,
        15,14,13,13, 11,14,10,12, 15,10,13,12, 11,14,9,12,
        8,10,13,8, 13,7,9,12, 9,12,11,10, 5,8,7,6,
        1,4,3,2
    },
    {
        3,0,0,0, 0,1,0,0, 4,5,6,0, 8,9,10,11,
        12,13,14,15, 16,17,18,19, 20,21,22,23, 24,25,26,27,
        28,29,30,31, 32,33,34,35, 36,37,38,39, 40,41,42,43,
        44,45,46,47, 48,49,50,51, 52,53,54,55, 56,57,58,59,
        60,61,62,63
    }
};

static const uint8_t chroma_dc_coeff_token_len[4 * 5] = {
    2,0,0,0, 6,1,0,0, 6,6,3,0, 6,7,7,6, 6,8,8,7
};

static const uint8_t chroma_dc_coeff_token_bits[4 * 5] = {
    1,0,0,0, 7,1,0,0, 4,6,1,0, 3,3,2,5, 2,3,2,0
};

static const uint8_t chroma422_dc_coeff_token_len[4 * 9] = {
    1,0,0,0, 7,2,0,0, 7,7,3,0, 9,7,7,5,
    9,9,7,6, 10,10,9,7, 11,11,10,7, 12,12,11,10,
    13,12,12,11
};

static const uint8_t chroma422_dc_coeff_token_bits[4 * 9] = {
    1,0,0,0, 15,1,0,0, 14,13,1,0, 7,12,11,1,
    6,5,10,1, 7,6,4,9, 7,6,5,8, 7,6,5,4,
    7,5,4,4
};

static const uint8_t total_zeros_len[15][16] = {
    {1,3,3,4,4,5,5,6,6,7,7,8,8,9,9,9},
    {3,3,3,3,3,4,4,4,4,5,5,6,6,6,6},
    {4,3,3,3,4,4,3,3,4,5,5,6,5,6},
    {5,3,4,4,3,3,3,4,3,4,5,5,5},
    {4,4,4,3,3,3,3,3,4,5,4,5},
    {6,5,3,3,3,3,3,3,4,3,6},
    {6,5,3,3,3,2,3,4,3,6},
    {6,4,5,3,2,2,3,3,6},
    {6,6,4,2,2,3,2,5},
    {5,5,3,2,2,2,4},
    {4,4,3,3,1,3},
    {4,4,2,1,3},
    {3,3,1,2},
    {2,2,1},
    {1,1}
};

static const uint8_t total_zeros_bits[15][16] = {
    {1,3,2,3,2,3,2,3,2,3,2,3,2,3,2,1},
    {7,6,5,4,3,5,4,3,2,3,2,3,2,1,0},
    {5,7,6,5,4,3,4,3,2,3,2,1,1,0},
    {3,7,5,4,6,5,4,3,3,2,2,1,0},
    {5,4,3,7,6,5,4,3,2,1,1,0},
    {1,1,7,6,5,4,3,2,1,1,0},
    {1,1,5,4,3,3,2,1,1,0},
    {1,1,1,3,3,2,2,1,0},
    {1,0,1,3,2,1,1,1},
    {1,0,1,3,2,1,1},
    {0,1,1,2,1,3},
    {0,1,1,1,1},
    {0,1,1,1},
    {0,1,1},
    {0,1}
};

static const uint8_t chroma_dc_total_zeros_len[3][4] = {
    {1,2,3,3},
    {1,2,2,0},
    {1,1,0,0}
};

static const uint8_t chroma_dc_total_zeros_bits[3][4] = {
    {1,1,1,0},
    {1,1,0,0},
    {1,0,0,0}
};

static const uint8_t chroma422_dc_total_zeros_len[7][8] = {
    {1,3,3,4,4,4,5,5},
    {3,2,3,3,3,3,3,0},
    {3,3,2,2,3,3,0,0},
    {3,2,2,2,3,0,0,0},
    {2,2,2,2,0,0,0,0},
    {2,2,1,0,0,0,0,0},
    {1,1,0,0,0,0,0,0}
};

static const uint8_t chroma422_dc_total_zeros_bits[7][8] = {
    {1,2,3,2,3,1,1,0},
    {0,1,1,4,5,6,7,0},
    {0,1,1,2,6,7,0,0},
    {6,0,1,2,7,0,0,0},
    {0,1,2,3,0,0,0,0},
    {0,1,1,0,0,0,0,0},
    {0,1,0,0,0,0,0,0}
};

static const uint8_t run_before_len[7][16] = {
    {1,1},
    {1,2,2},
    {2,2,2,2},
    {2,2,2,3,3},
    {2,2,3,3,3,3},
    {2,3,3,3,3,3,3},
    {3,3,3,3,3,3,3,4,5,6,7,8,9,10,11}
};

static const uint8_t run_before_bits[7][16] = {
    {1,0},
    {1,1,0},
    {3,2,1,0},
    {3,2,1,1,0},
    {3,2,3,2,1,0},
    {3,0,1,3,2,5,4},
    {7,6,5,4,3,2,1,1,1,1,1,1,1,1,1}
};

static int read_level_suffix(avc_bitreader_t *br, unsigned suffix_length)
{
    unsigned level_prefix = 0;
    unsigned level_suffix = 0;
    int level_code;

    while (avc_br_bits_left(br) > 0 && avc_br_read_bit(br) == 0) {
        level_prefix++;
        if (level_prefix > 31) {
            br->error = 1;
            return 0;
        }
    }

    if (level_prefix == 14 && suffix_length == 0) {
        level_suffix = avc_br_read_bits(br, 4);
    } else if (level_prefix >= 15) {
        level_suffix = avc_br_read_bits(br, level_prefix - 3);
    } else if (suffix_length > 0) {
        level_suffix = avc_br_read_bits(br, suffix_length);
    }

    level_code = (int)((level_prefix << suffix_length) + level_suffix);
    if (level_prefix == 15 && suffix_length == 0) {
        level_code += 15;
    }
    return (level_code & 1) ? -((level_code + 1) >> 1) : ((level_code + 2) >> 1);
}

static int match_coeff_token_table(avc_bitreader_t *br,
                                   const uint8_t *len_table,
                                   const uint8_t *bits_table,
                                   unsigned total_coeff_count,
                                   unsigned *total_coeff,
                                   unsigned *trailing_ones)
{
    unsigned code = 0;
    unsigned len;
    unsigned i;
    unsigned max_len = 0;

    for (i = 0; i < total_coeff_count * 4u; i++) {
        if (len_table[i] > max_len) {
            max_len = len_table[i];
        }
    }

    for (len = 1; len <= max_len; len++) {
        if (avc_br_bits_left(br) == 0) {
            br->error = 1;
            return 0;
        }
        code = (code << 1) | avc_br_read_bit(br);
        if (br->error) {
            return 0;
        }
        for (i = 0; i < total_coeff_count * 4u; i++) {
            if (len_table[i] == len && bits_table[i] == code) {
                *total_coeff = i / 4u;
                *trailing_ones = i & 3u;
                return 1;
            }
        }
    }

    br->error = 1;
    return 0;
}

static int match_vlc_table(avc_bitreader_t *br,
                           const uint8_t *len_table,
                           const uint8_t *bits_table,
                           unsigned value_count,
                           unsigned *value)
{
    unsigned code = 0;
    unsigned len;
    unsigned i;
    unsigned max_len = 0;

    for (i = 0; i < value_count; i++) {
        if (len_table[i] > max_len) {
            max_len = len_table[i];
        }
    }

    for (len = 1; len <= max_len; len++) {
        if (avc_br_bits_left(br) == 0) {
            br->error = 1;
            return 0;
        }
        code = (code << 1) | avc_br_read_bit(br);
        if (br->error) {
            return 0;
        }
        for (i = 0; i < value_count; i++) {
            if (len_table[i] == len && bits_table[i] == code) {
                *value = i;
                return 1;
            }
        }
    }

    br->error = 1;
    return 0;
}

static int read_coeff_token(avc_bitreader_t *br, int nC,
                            unsigned max_coeff,
                            unsigned *total_coeff,
                            unsigned *trailing_ones)
{
    unsigned table_index;
    unsigned code;

    if (max_coeff == 4) {
        return match_coeff_token_table(br, chroma_dc_coeff_token_len,
                                       chroma_dc_coeff_token_bits, 5,
                                       total_coeff, trailing_ones);
    }
    if (max_coeff == 8) {
        return match_coeff_token_table(br, chroma422_dc_coeff_token_len,
                                       chroma422_dc_coeff_token_bits, 9,
                                       total_coeff, trailing_ones);
    }

    if (nC < 2) {
        table_index = 0;
    } else if (nC < 4) {
        table_index = 1;
    } else if (nC < 8) {
        table_index = 2;
    } else {
        code = avc_br_read_bits(br, 6);
        if (br->error) {
            return 0;
        }
        *trailing_ones = code & 3u;
        *total_coeff = code >> 2;
        if (*total_coeff == 0 && *trailing_ones == 3) {
            *trailing_ones = 0;
        } else {
            (*total_coeff)++;
        }
        return 1;
    }

    return match_coeff_token_table(br, coeff_token_len[table_index],
                                   coeff_token_bits[table_index], 17,
                                   total_coeff, trailing_ones);
}

static int read_total_zeros(avc_bitreader_t *br,
                            unsigned max_coeff,
                            unsigned total_coeff,
                            unsigned *total_zeros)
{
    unsigned value_count;

    if (total_coeff == 0 || total_coeff > max_coeff) {
        br->error = 1;
        return 0;
    }
    if (total_coeff == max_coeff) {
        *total_zeros = 0;
        return 1;
    }

    value_count = max_coeff - total_coeff + 1u;
    if (max_coeff == 4) {
        return match_vlc_table(br, chroma_dc_total_zeros_len[total_coeff - 1u],
                               chroma_dc_total_zeros_bits[total_coeff - 1u],
                               value_count, total_zeros);
    }
    if (max_coeff == 8) {
        return match_vlc_table(br, chroma422_dc_total_zeros_len[total_coeff - 1u],
                               chroma422_dc_total_zeros_bits[total_coeff - 1u],
                               value_count, total_zeros);
    }
    if ((max_coeff != 15 && max_coeff != 16) || total_coeff > 15) {
        br->error = 1;
        return 0;
    }
    return match_vlc_table(br, total_zeros_len[total_coeff - 1u],
                           total_zeros_bits[total_coeff - 1u],
                           value_count, total_zeros);
}

static int read_run_before(avc_bitreader_t *br, unsigned zeros_left,
                           unsigned *run_before)
{
    unsigned table;
    unsigned value_count;

    if (zeros_left == 0) {
        *run_before = 0;
        return 1;
    }

    table = zeros_left > 6u ? 6u : zeros_left - 1u;
    value_count = zeros_left > 6u ? 15u : zeros_left + 1u;
    return match_vlc_table(br, run_before_len[table], run_before_bits[table],
                           value_count, run_before);
}

int avc_cavlc_scan_position(unsigned max_coeff,
                            unsigned scan,
                            avc_cavlc_scan_t scan_mode,
                            unsigned *x,
                            unsigned *y)
{
    static const uint8_t scan_4x4_frame[16] = {
        0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15
    };
    static const uint8_t scan_4x4_field[16] = {
        0, 4, 1, 8, 12, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15
    };
    static const uint8_t scan_8x8_frame[64] = {
        0, 1, 8, 16, 9, 2, 3, 10,
        17, 24, 32, 25, 18, 11, 4, 5,
        12, 19, 26, 33, 40, 48, 41, 34,
        27, 20, 13, 6, 7, 14, 21, 28,
        35, 42, 49, 56, 57, 50, 43, 36,
        29, 22, 15, 23, 30, 37, 44, 51,
        58, 59, 52, 45, 38, 31, 39, 46,
        53, 60, 61, 54, 47, 55, 62, 63
    };
    static const uint8_t scan_8x8_field[64] = {
        0, 8, 16, 1, 9, 24, 32, 17,
        2, 10, 25, 40, 48, 33, 18, 3,
        11, 26, 41, 56, 49, 34, 19, 4,
        12, 27, 42, 57, 50, 35, 20, 5,
        13, 28, 43, 58, 51, 36, 21, 6,
        14, 29, 44, 59, 52, 37, 22, 7,
        15, 30, 45, 60, 53, 38, 23, 31,
        46, 61, 54, 39, 47, 62, 55, 63
    };
    int field_scan = scan_mode == AVC_CAVLC_SCAN_FIELD ||
                     scan_mode == AVC_CAVLC_SCAN_TRANSFORM_BYPASS_FIELD;
    unsigned raster;

    if (!x || !y) {
        return 0;
    }

    if (max_coeff == 64) {
        if (scan >= 64) {
            return 0;
        }
        raster = field_scan ? scan_8x8_field[scan] : scan_8x8_frame[scan];
        *x = raster & 7u;
        *y = raster >> 3;
        return 1;
    }
    if (max_coeff == 16) {
        if (scan >= 16) {
            return 0;
        }
        raster = field_scan ? scan_4x4_field[scan] : scan_4x4_frame[scan];
        *x = raster & 3u;
        *y = raster >> 2;
        return 1;
    }
    if (max_coeff == 15) {
        if (scan >= 15) {
            return 0;
        }
        raster = field_scan ? scan_4x4_field[scan + 1u] : scan_4x4_frame[scan + 1u];
        *x = raster & 3u;
        *y = raster >> 2;
        return 1;
    }
    if (max_coeff == 4) {
        if (scan >= 4) {
            return 0;
        }
        *x = scan & 1u;
        *y = scan >> 1;
        return 1;
    }
    if (max_coeff == 8) {
        if (scan >= 8) {
            return 0;
        }
        *x = scan & 1u;
        *y = scan >> 1;
        return 1;
    }
    return 0;
}

int avc_cavlc_read_residual_block(avc_bitreader_t *br, int nC, unsigned max_coeff,
                                  avc_cavlc_scan_t scan_mode,
                                  avc_cavlc_block_t *block,
                                  avc_cavlc_callbacks_t callbacks,
                                  void *opaque)
{
    unsigned i;
    unsigned suffix_length = 0;
    unsigned zeros_left = 0;

    if (max_coeff > AVC_CAVLC_MAX_COEFFS) {
        br->error = 1;
        return 0;
    }

    memset(block, 0, sizeof(*block));
    if (!read_coeff_token(br, nC, max_coeff,
                          &block->total_coeff, &block->trailing_ones)) {
        return 0;
    }
    if (block->total_coeff > max_coeff || block->trailing_ones > 3 ||
        block->trailing_ones > block->total_coeff) {
        br->error = 1;
        return 0;
    }

    for (i = 0; i < block->trailing_ones; i++) {
        block->coeff_level[i] = avc_br_read_bit(br) ? -1 : 1;
    }

    if (block->total_coeff > 10 && block->trailing_ones < 3) {
        suffix_length = 1;
    }
    for (i = block->trailing_ones; i < block->total_coeff; i++) {
        int level = read_level_suffix(br, suffix_length);
        block->coeff_level[i] = level;
        if (suffix_length == 0) {
            suffix_length = 1;
        }
        if ((level > (3 << (suffix_length - 1))) && suffix_length < 6) {
            suffix_length++;
        }
    }

    if (block->total_coeff == 0) {
        return !br->error;
    }

    if (!read_total_zeros(br, max_coeff, block->total_coeff, &block->total_zeros)) {
        return 0;
    }
    zeros_left = block->total_zeros;

    for (i = 0; i + 1u < block->total_coeff && zeros_left > 0; i++) {
        if (!read_run_before(br, zeros_left, &block->run_before[i])) {
            return 0;
        }
        if (block->run_before[i] > zeros_left) {
            br->error = 1;
            return 0;
        }
        zeros_left -= block->run_before[i];
    }
    if (block->total_coeff > 0) {
        block->run_before[block->total_coeff - 1u] = zeros_left;
    }

    {
        unsigned scan = block->total_zeros + block->total_coeff - 1u;
        for (i = 0; i < block->total_coeff; i++) {
            if (scan >= max_coeff) {
                br->error = 1;
                return 0;
            }
            block->coeff_scan[i] = scan;
            if (!avc_cavlc_scan_position(max_coeff, scan, scan_mode,
                                         &block->coeff_x[i], &block->coeff_y[i])) {
                br->error = 1;
                return 0;
            }
            if (callbacks.on_coeff) {
                callbacks.on_coeff(opaque, scan, block->coeff_level[i],
                                   block->run_before[i]);
            }
            if (i + 1u < block->total_coeff) {
                if (scan < block->run_before[i] + 1u) {
                    br->error = 1;
                    return 0;
                }
                scan -= block->run_before[i] + 1u;
            }
        }
    }

    return !br->error;
}
