#include "avc/avc_sei.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint8_t data[256];
    size_t bit_pos;
} bit_writer_t;

typedef struct {
    unsigned count;
    avc_sei_event_t last;
} sei_trace_t;

static void bw_put_bit(bit_writer_t *bw, unsigned bit)
{
    if (bit) {
        bw->data[bw->bit_pos >> 3] |= (uint8_t)(1u << (7u - (bw->bit_pos & 7u)));
    }
    bw->bit_pos++;
}

static void bw_put_bits(bit_writer_t *bw, uint32_t value, unsigned bits)
{
    unsigned i;
    for (i = 0; i < bits; i++) {
        bw_put_bit(bw, (value >> (bits - 1u - i)) & 1u);
    }
}

static void bw_put_ue(bit_writer_t *bw, uint32_t value)
{
    uint32_t code_num = value + 1u;
    unsigned bits = 0;
    uint32_t tmp = code_num;
    unsigned i;

    while (tmp != 0) {
        bits++;
        tmp >>= 1;
    }
    for (i = 0; i + 1u < bits; i++) {
        bw_put_bit(bw, 0);
    }
    bw_put_bits(bw, code_num, bits);
}

static size_t bw_bytes(bit_writer_t *bw)
{
    return (bw->bit_pos + 7u) >> 3;
}

static size_t write_sei_payload(uint8_t *dst, uint32_t type,
                                const uint8_t *payload, size_t payload_size)
{
    size_t pos = 0;
    while (type >= 255u) {
        dst[pos++] = 0xff;
        type -= 255u;
    }
    dst[pos++] = (uint8_t)type;
    while (payload_size >= 255u) {
        dst[pos++] = 0xff;
        payload_size -= 255u;
    }
    dst[pos++] = (uint8_t)payload_size;
    memcpy(&dst[pos], payload, payload_size);
    pos += payload_size;
    dst[pos++] = 0x80;
    return pos;
}

static void on_sei(void *opaque, const avc_sei_event_t *sei)
{
    sei_trace_t *trace = (sei_trace_t *)opaque;
    trace->count++;
    trace->last = *sei;
}

static void test_recovery_point(void)
{
    uint8_t rbsp[32];
    bit_writer_t payload = {0};
    sei_trace_t trace = {0};
    avc_sei_callbacks_t callbacks = {0};
    size_t size;

    bw_put_ue(&payload, 5);
    bw_put_bit(&payload, 1);
    bw_put_bit(&payload, 0);
    bw_put_bits(&payload, 2, 2);
    size = write_sei_payload(rbsp, AVC_SEI_RECOVERY_POINT, payload.data, bw_bytes(&payload));

    callbacks.on_sei = on_sei;
    assert(avc_parse_sei_rbsp(rbsp, size, NULL, NULL, callbacks, &trace));
    assert(trace.count == 1);
    assert(trace.last.parsed);
    assert(trace.last.payload_type == AVC_SEI_RECOVERY_POINT);
    assert(trace.last.recovery_point.recovery_frame_cnt == 5);
    assert(trace.last.recovery_point.exact_match_flag == 1);
    assert(trace.last.recovery_point.broken_link_flag == 0);
    assert(trace.last.recovery_point.changing_slice_group_idc == 2);
}

static void init_timing_sps(avc_parameter_sets_t *sets)
{
    avc_sps_t *sps;

    *sets = (avc_parameter_sets_t){0};
    sps = &sets->sps[0];
    sps->present = 1;
    sps->seq_parameter_set_id = 0;
    sps->vui_parameters_present_flag = 1;
    sps->vui.nal_hrd_parameters_present_flag = 1;
    sps->vui.nal_hrd_parameters.present = 1;
    sps->vui.nal_hrd_parameters.cpb_cnt_minus1 = 0;
    sps->vui.nal_hrd_parameters.initial_cpb_removal_delay_length_minus1 = 7;
    sps->vui.nal_hrd_parameters.cpb_removal_delay_length_minus1 = 4;
    sps->vui.nal_hrd_parameters.dpb_output_delay_length_minus1 = 3;
    sps->vui.nal_hrd_parameters.time_offset_length = 4;
    sps->vui.pic_struct_present_flag = 1;
}

static void test_buffering_period(void)
{
    uint8_t rbsp[32];
    bit_writer_t payload = {0};
    avc_parameter_sets_t sets;
    sei_trace_t trace = {0};
    avc_sei_callbacks_t callbacks = {0};
    size_t size;

    init_timing_sps(&sets);
    bw_put_ue(&payload, 0);
    bw_put_bits(&payload, 12, 8);
    bw_put_bits(&payload, 34, 8);
    size = write_sei_payload(rbsp, AVC_SEI_BUFFERING_PERIOD, payload.data, bw_bytes(&payload));

    callbacks.on_sei = on_sei;
    assert(avc_parse_sei_rbsp(rbsp, size, &sets, NULL, callbacks, &trace));
    assert(trace.count == 1);
    assert(trace.last.parsed);
    assert(trace.last.buffering_period.seq_parameter_set_id == 0);
    assert(trace.last.buffering_period.nal_initial_cpb_removal_delay[0] == 12);
    assert(trace.last.buffering_period.nal_initial_cpb_removal_delay_offset[0] == 34);
}

static void test_pic_timing(void)
{
    uint8_t rbsp[32];
    bit_writer_t payload = {0};
    avc_parameter_sets_t sets;
    sei_trace_t trace = {0};
    avc_sei_callbacks_t callbacks = {0};
    size_t size;

    init_timing_sps(&sets);
    bw_put_bits(&payload, 17, 5);
    bw_put_bits(&payload, 9, 4);
    bw_put_bits(&payload, 0, 4);
    bw_put_bit(&payload, 1);
    bw_put_bits(&payload, 2, 2);
    bw_put_bit(&payload, 1);
    bw_put_bits(&payload, 3, 5);
    bw_put_bit(&payload, 1);
    bw_put_bit(&payload, 0);
    bw_put_bit(&payload, 1);
    bw_put_bits(&payload, 7, 8);
    bw_put_bits(&payload, 12, 6);
    bw_put_bits(&payload, 34, 6);
    bw_put_bits(&payload, 5, 5);
    bw_put_bits(&payload, 0x0e, 4);
    size = write_sei_payload(rbsp, AVC_SEI_PIC_TIMING, payload.data, bw_bytes(&payload));

    callbacks.on_sei = on_sei;
    assert(avc_parse_sei_rbsp(rbsp, size, &sets, NULL, callbacks, &trace));
    assert(trace.count == 1);
    assert(trace.last.parsed);
    assert(trace.last.pic_timing.cpb_removal_delay == 17);
    assert(trace.last.pic_timing.dpb_output_delay == 9);
    assert(trace.last.pic_timing.pic_struct == 0);
    assert(trace.last.pic_timing.clock_timestamp_count == 1);
    assert(trace.last.pic_timing.clock_timestamp[0].present == 1);
    assert(trace.last.pic_timing.clock_timestamp[0].ct_type == 2);
    assert(trace.last.pic_timing.clock_timestamp[0].counting_type == 3);
    assert(trace.last.pic_timing.clock_timestamp[0].n_frames == 7);
    assert(trace.last.pic_timing.clock_timestamp[0].seconds_value == 12);
    assert(trace.last.pic_timing.clock_timestamp[0].minutes_value == 34);
    assert(trace.last.pic_timing.clock_timestamp[0].hours_value == 5);
    assert(trace.last.pic_timing.clock_timestamp[0].time_offset == -2);
}

int main(void)
{
    test_recovery_point();
    test_buffering_period();
    test_pic_timing();
    return 0;
}
