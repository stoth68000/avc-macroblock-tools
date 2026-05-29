#include "avc/avc_nal.h"

static int start_code_len_at(const uint8_t *data, size_t size, size_t pos)
{
    if (pos + 3 <= size && data[pos] == 0 && data[pos + 1] == 0 && data[pos + 2] == 1) {
        return 3;
    }
    if (pos + 4 <= size && data[pos] == 0 && data[pos + 1] == 0 && data[pos + 2] == 0 && data[pos + 3] == 1) {
        return 4;
    }
    return 0;
}

static int find_start_code(const uint8_t *data, size_t size, size_t from, size_t *pos, int *len)
{
    size_t i;
    for (i = from; i + 3 <= size; i++) {
        int n = start_code_len_at(data, size, i);
        if (n) {
            *pos = i;
            *len = n;
            return 1;
        }
    }
    return 0;
}

void avc_annexb_reader_init(avc_annexb_reader_t *reader, const uint8_t *data, size_t size)
{
    reader->data = data;
    reader->size = size;
    reader->cursor = 0;
}

int avc_annexb_next(avc_annexb_reader_t *reader, avc_nal_unit_t *nal)
{
    size_t start;
    size_t next;
    int start_len;
    int next_len;
    size_t nal_start;
    size_t nal_end;

    if (!find_start_code(reader->data, reader->size, reader->cursor, &start, &start_len)) {
        return 0;
    }

    nal_start = start + (size_t)start_len;
    if (!find_start_code(reader->data, reader->size, nal_start, &next, &next_len)) {
        nal_end = reader->size;
        reader->cursor = reader->size;
    } else {
        nal_end = next;
        reader->cursor = next;
    }

    while (nal_end > nal_start && reader->data[nal_end - 1] == 0) {
        nal_end--;
    }

    if (nal_end <= nal_start) {
        return avc_annexb_next(reader, nal);
    }

    if (!avc_nal_parse_header(reader->data[nal_start], &nal->header)) {
        return 0;
    }

    nal->ebsp = reader->data + nal_start + 1;
    nal->ebsp_size = nal_end - nal_start - 1;
    nal->offset = start;
    return 1;
}

int avc_nal_parse_header(uint8_t byte, avc_nal_header_t *header)
{
    header->forbidden_zero_bit = (byte >> 7) & 1u;
    header->nal_ref_idc = (byte >> 5) & 3u;
    header->nal_unit_type = byte & 31u;
    return header->forbidden_zero_bit == 0;
}

const char *avc_nal_type_name(uint8_t nal_unit_type)
{
    switch (nal_unit_type) {
    case AVC_NAL_SLICE_NON_IDR: return "non_idr_slice";
    case AVC_NAL_SLICE_PARTITION_A: return "partition_a";
    case AVC_NAL_SLICE_PARTITION_B: return "partition_b";
    case AVC_NAL_SLICE_PARTITION_C: return "partition_c";
    case AVC_NAL_SLICE_IDR: return "idr_slice";
    case AVC_NAL_SEI: return "sei";
    case AVC_NAL_SPS: return "sps";
    case AVC_NAL_PPS: return "pps";
    case AVC_NAL_AUD: return "aud";
    case AVC_NAL_END_SEQUENCE: return "end_sequence";
    case AVC_NAL_END_STREAM: return "end_stream";
    case AVC_NAL_FILLER: return "filler";
    default: return "unspecified";
    }
}

size_t avc_ebsp_to_rbsp(const uint8_t *ebsp, size_t ebsp_size, uint8_t *rbsp, size_t rbsp_capacity)
{
    size_t i;
    size_t out = 0;
    unsigned zero_count = 0;

    for (i = 0; i < ebsp_size; i++) {
        uint8_t b = ebsp[i];
        if (zero_count == 2 && b == 0x03) {
            zero_count = 0;
            continue;
        }
        if (out >= rbsp_capacity) {
            return 0;
        }
        rbsp[out++] = b;
        if (b == 0) {
            zero_count++;
        } else {
            zero_count = 0;
        }
    }
    return out;
}
