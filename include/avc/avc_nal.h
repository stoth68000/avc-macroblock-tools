#ifndef AVC_NAL_H
#define AVC_NAL_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    AVC_NAL_UNSPECIFIED = 0,
    AVC_NAL_SLICE_NON_IDR = 1,
    AVC_NAL_SLICE_PARTITION_A = 2,
    AVC_NAL_SLICE_PARTITION_B = 3,
    AVC_NAL_SLICE_PARTITION_C = 4,
    AVC_NAL_SLICE_IDR = 5,
    AVC_NAL_SEI = 6,
    AVC_NAL_SPS = 7,
    AVC_NAL_PPS = 8,
    AVC_NAL_AUD = 9,
    AVC_NAL_END_SEQUENCE = 10,
    AVC_NAL_END_STREAM = 11,
    AVC_NAL_FILLER = 12
} avc_nal_unit_type_t;

typedef struct {
    uint8_t forbidden_zero_bit;
    uint8_t nal_ref_idc;
    uint8_t nal_unit_type;
} avc_nal_header_t;

typedef struct {
    avc_nal_header_t header;
    const uint8_t *ebsp;
    size_t ebsp_size;
    size_t offset;
} avc_nal_unit_t;

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t cursor;
} avc_annexb_reader_t;

void avc_annexb_reader_init(avc_annexb_reader_t *reader, const uint8_t *data, size_t size);
int avc_annexb_next(avc_annexb_reader_t *reader, avc_nal_unit_t *nal);
int avc_nal_parse_header(uint8_t byte, avc_nal_header_t *header);
const char *avc_nal_type_name(uint8_t nal_unit_type);
size_t avc_ebsp_to_rbsp(const uint8_t *ebsp, size_t ebsp_size, uint8_t *rbsp, size_t rbsp_capacity);

#endif
