#ifndef AVC_SEI_H
#define AVC_SEI_H

#include <stddef.h>
#include <stdint.h>
#include "avc/avc_syntax.h"

#define AVC_SEI_MAX_CPB_DELAYS AVC_MAX_HRD_CPB_CNT
#define AVC_SEI_MAX_CLOCK_TIMESTAMPS 3

typedef enum {
    AVC_SEI_BUFFERING_PERIOD = 0,
    AVC_SEI_PIC_TIMING = 1,
    AVC_SEI_RECOVERY_POINT = 6
} avc_sei_payload_type_t;

typedef struct {
    uint8_t present;
    uint8_t ct_type;
    uint8_t nuit_field_based_flag;
    uint8_t counting_type;
    uint8_t full_timestamp_flag;
    uint8_t discontinuity_flag;
    uint8_t cnt_dropped_flag;
    uint8_t n_frames;
    uint8_t seconds_flag;
    uint8_t minutes_flag;
    uint8_t hours_flag;
    uint8_t seconds_value;
    uint8_t minutes_value;
    uint8_t hours_value;
    int32_t time_offset;
} avc_sei_clock_timestamp_t;

typedef struct {
    uint32_t seq_parameter_set_id;
    uint32_t nal_initial_cpb_removal_delay[AVC_SEI_MAX_CPB_DELAYS];
    uint32_t nal_initial_cpb_removal_delay_offset[AVC_SEI_MAX_CPB_DELAYS];
    uint32_t vcl_initial_cpb_removal_delay[AVC_SEI_MAX_CPB_DELAYS];
    uint32_t vcl_initial_cpb_removal_delay_offset[AVC_SEI_MAX_CPB_DELAYS];
} avc_sei_buffering_period_t;

typedef struct {
    uint8_t cpb_removal_delay_present;
    uint8_t dpb_output_delay_present;
    uint8_t pic_struct_present;
    uint32_t cpb_removal_delay;
    uint32_t dpb_output_delay;
    uint8_t pic_struct;
    uint8_t clock_timestamp_count;
    avc_sei_clock_timestamp_t clock_timestamp[AVC_SEI_MAX_CLOCK_TIMESTAMPS];
} avc_sei_pic_timing_t;

typedef struct {
    uint32_t recovery_frame_cnt;
    uint8_t exact_match_flag;
    uint8_t broken_link_flag;
    uint8_t changing_slice_group_idc;
} avc_sei_recovery_point_t;

typedef struct {
    uint32_t payload_type;
    size_t payload_size;
    uint8_t parsed;
    avc_sei_buffering_period_t buffering_period;
    avc_sei_pic_timing_t pic_timing;
    avc_sei_recovery_point_t recovery_point;
} avc_sei_event_t;

typedef struct {
    void (*on_sei)(void *opaque, const avc_sei_event_t *sei);
} avc_sei_callbacks_t;

int avc_parse_sei_rbsp(const uint8_t *rbsp, size_t rbsp_size,
                       const avc_parameter_sets_t *sets,
                       const avc_sps_t *active_sps,
                       avc_sei_callbacks_t callbacks,
                       void *opaque);

#endif
