#include "avc/avc_sei.h"
#include "avc/avc_bitreader.h"

static unsigned clock_timestamp_count(uint8_t pic_struct)
{
    switch (pic_struct) {
    case 0: case 1: case 2:
        return 1;
    case 3: case 4: case 7:
        return 2;
    case 5: case 6: case 8:
        return 3;
    default:
        return 0;
    }
}

static int hrd_delays_present(const avc_sps_t *sps)
{
    return sps && sps->vui_parameters_present_flag &&
           (sps->vui.nal_hrd_parameters_present_flag ||
            sps->vui.vcl_hrd_parameters_present_flag);
}

static const avc_sps_t *find_sps(const avc_parameter_sets_t *sets,
                                 uint32_t sps_id)
{
    if (!sets || sps_id >= AVC_MAX_SPS || !sets->sps[sps_id].present) {
        return NULL;
    }
    return &sets->sps[sps_id];
}

static const avc_sps_t *infer_active_sps(const avc_parameter_sets_t *sets,
                                         const avc_sps_t *active_sps)
{
    const avc_sps_t *found = NULL;
    unsigned i;

    if (active_sps && active_sps->present) {
        return active_sps;
    }
    if (!sets) {
        return NULL;
    }
    for (i = 0; i < AVC_MAX_SPS; i++) {
        if (sets->sps[i].present) {
            if (found) {
                return NULL;
            }
            found = &sets->sps[i];
        }
    }
    return found;
}

static uint32_t read_fixed(avc_bitreader_t *br, unsigned bits)
{
    if (bits == 0) {
        return 0;
    }
    return avc_br_read_bits(br, bits);
}

static int32_t read_signed_fixed(avc_bitreader_t *br, unsigned bits)
{
    uint32_t value;
    uint32_t sign_bit;

    if (bits == 0) {
        return 0;
    }
    value = avc_br_read_bits(br, bits);
    if (bits >= 32) {
        return (int32_t)value;
    }
    sign_bit = 1u << (bits - 1u);
    if (value & sign_bit) {
        value |= ~((1u << bits) - 1u);
    }
    return (int32_t)value;
}

static int parse_buffering_period(avc_bitreader_t *br,
                                  const avc_parameter_sets_t *sets,
                                  avc_sei_event_t *event)
{
    const avc_sps_t *sps;
    unsigned i;

    event->buffering_period.seq_parameter_set_id = avc_br_read_ue(br);
    sps = find_sps(sets, event->buffering_period.seq_parameter_set_id);
    if (!sps) {
        return 0;
    }
    if (sps->vui.nal_hrd_parameters_present_flag) {
        unsigned bits = sps->vui.nal_hrd_parameters.initial_cpb_removal_delay_length_minus1 + 1u;
        for (i = 0; i <= sps->vui.nal_hrd_parameters.cpb_cnt_minus1; i++) {
            event->buffering_period.nal_initial_cpb_removal_delay[i] = read_fixed(br, bits);
            event->buffering_period.nal_initial_cpb_removal_delay_offset[i] = read_fixed(br, bits);
        }
    }
    if (sps->vui.vcl_hrd_parameters_present_flag) {
        unsigned bits = sps->vui.vcl_hrd_parameters.initial_cpb_removal_delay_length_minus1 + 1u;
        for (i = 0; i <= sps->vui.vcl_hrd_parameters.cpb_cnt_minus1; i++) {
            event->buffering_period.vcl_initial_cpb_removal_delay[i] = read_fixed(br, bits);
            event->buffering_period.vcl_initial_cpb_removal_delay_offset[i] = read_fixed(br, bits);
        }
    }
    return !br->error;
}

static int parse_pic_timing(avc_bitreader_t *br,
                            const avc_parameter_sets_t *sets,
                            const avc_sps_t *active_sps,
                            avc_sei_event_t *event)
{
    const avc_sps_t *sps = infer_active_sps(sets, active_sps);
    unsigned count;
    unsigned i;

    if (!sps) {
        return 0;
    }
    if (hrd_delays_present(sps)) {
        const avc_hrd_parameters_t *hrd = sps->vui.nal_hrd_parameters_present_flag ?
            &sps->vui.nal_hrd_parameters : &sps->vui.vcl_hrd_parameters;
        event->pic_timing.cpb_removal_delay_present = 1;
        event->pic_timing.cpb_removal_delay =
            read_fixed(br, hrd->cpb_removal_delay_length_minus1 + 1u);
        event->pic_timing.dpb_output_delay_present = 1;
        event->pic_timing.dpb_output_delay =
            read_fixed(br, hrd->dpb_output_delay_length_minus1 + 1u);
    }
    if (!sps->vui.pic_struct_present_flag) {
        return !br->error;
    }
    event->pic_timing.pic_struct_present = 1;
    event->pic_timing.pic_struct = (uint8_t)avc_br_read_bits(br, 4);
    count = clock_timestamp_count(event->pic_timing.pic_struct);
    event->pic_timing.clock_timestamp_count = (uint8_t)count;
    if (count > AVC_SEI_MAX_CLOCK_TIMESTAMPS) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        avc_sei_clock_timestamp_t *ts = &event->pic_timing.clock_timestamp[i];
        ts->present = (uint8_t)avc_br_read_bit(br);
        if (!ts->present) {
            continue;
        }
        ts->ct_type = (uint8_t)avc_br_read_bits(br, 2);
        ts->nuit_field_based_flag = (uint8_t)avc_br_read_bit(br);
        ts->counting_type = (uint8_t)avc_br_read_bits(br, 5);
        ts->full_timestamp_flag = (uint8_t)avc_br_read_bit(br);
        ts->discontinuity_flag = (uint8_t)avc_br_read_bit(br);
        ts->cnt_dropped_flag = (uint8_t)avc_br_read_bit(br);
        ts->n_frames = (uint8_t)avc_br_read_bits(br, 8);
        if (ts->full_timestamp_flag) {
            ts->seconds_flag = 1;
            ts->minutes_flag = 1;
            ts->hours_flag = 1;
            ts->seconds_value = (uint8_t)avc_br_read_bits(br, 6);
            ts->minutes_value = (uint8_t)avc_br_read_bits(br, 6);
            ts->hours_value = (uint8_t)avc_br_read_bits(br, 5);
        } else if ((ts->seconds_flag = (uint8_t)avc_br_read_bit(br)) != 0) {
            ts->seconds_value = (uint8_t)avc_br_read_bits(br, 6);
            if ((ts->minutes_flag = (uint8_t)avc_br_read_bit(br)) != 0) {
                ts->minutes_value = (uint8_t)avc_br_read_bits(br, 6);
                if ((ts->hours_flag = (uint8_t)avc_br_read_bit(br)) != 0) {
                    ts->hours_value = (uint8_t)avc_br_read_bits(br, 5);
                }
            }
        }
        if (sps->vui.nal_hrd_parameters_present_flag) {
            ts->time_offset = read_signed_fixed(br, sps->vui.nal_hrd_parameters.time_offset_length);
        } else if (sps->vui.vcl_hrd_parameters_present_flag) {
            ts->time_offset = read_signed_fixed(br, sps->vui.vcl_hrd_parameters.time_offset_length);
        }
    }
    return !br->error;
}

static int parse_recovery_point(avc_bitreader_t *br,
                                avc_sei_event_t *event)
{
    event->recovery_point.recovery_frame_cnt = avc_br_read_ue(br);
    event->recovery_point.exact_match_flag = (uint8_t)avc_br_read_bit(br);
    event->recovery_point.broken_link_flag = (uint8_t)avc_br_read_bit(br);
    event->recovery_point.changing_slice_group_idc = (uint8_t)avc_br_read_bits(br, 2);
    return !br->error;
}

static int parse_known_payload(const uint8_t *payload,
                               size_t payload_size,
                               const avc_parameter_sets_t *sets,
                               const avc_sps_t *active_sps,
                               avc_sei_event_t *event)
{
    avc_bitreader_t br;
    int ok = 0;

    avc_br_init(&br, payload, payload_size);
    if (event->payload_type == AVC_SEI_BUFFERING_PERIOD) {
        ok = parse_buffering_period(&br, sets, event);
    } else if (event->payload_type == AVC_SEI_PIC_TIMING) {
        ok = parse_pic_timing(&br, sets, active_sps, event);
    } else if (event->payload_type == AVC_SEI_RECOVERY_POINT) {
        ok = parse_recovery_point(&br, event);
    }
    event->parsed = (uint8_t)(ok && !br.error);
    return event->parsed;
}

int avc_parse_sei_rbsp(const uint8_t *rbsp, size_t rbsp_size,
                       const avc_parameter_sets_t *sets,
                       const avc_sps_t *active_sps,
                       avc_sei_callbacks_t callbacks,
                       void *opaque)
{
    size_t pos = 0;

    while (pos < rbsp_size) {
        avc_sei_event_t event;
        uint32_t payload_type = 0;
        size_t payload_size = 0;
        int has_payload_type = 0;

        while (pos < rbsp_size && rbsp[pos] == 0xff) {
            payload_type += 255u;
            pos++;
            has_payload_type = 1;
        }
        if (pos >= rbsp_size) {
            break;
        }
        if (!has_payload_type && rbsp[pos] == 0x80) {
            break;
        }
        payload_type += rbsp[pos++];

        while (pos < rbsp_size && rbsp[pos] == 0xff) {
            payload_size += 255u;
            pos++;
        }
        if (pos >= rbsp_size) {
            return 0;
        }
        payload_size += rbsp[pos++];
        if (payload_size > rbsp_size - pos) {
            return 0;
        }

        event = (avc_sei_event_t){0};
        event.payload_type = payload_type;
        event.payload_size = payload_size;
        parse_known_payload(&rbsp[pos], payload_size, sets, active_sps, &event);
        if (callbacks.on_sei) {
            callbacks.on_sei(opaque, &event);
        }
        pos += payload_size;
    }
    return 1;
}
