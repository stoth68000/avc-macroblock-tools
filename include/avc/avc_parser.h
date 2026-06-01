#ifndef AVC_PARSER_H
#define AVC_PARSER_H

#include <stddef.h>
#include <stdint.h>
#include "avc/avc_dpb.h"
#include "avc/avc_macroblock.h"
#include "avc/avc_sei.h"
#include "avc/avc_syntax.h"

typedef struct avc_parser avc_parser_t;

typedef struct {
    void (*on_nal)(void *opaque, const avc_nal_unit_t *nal);
    void (*on_sps)(void *opaque, const avc_sps_t *sps);
    void (*on_pps)(void *opaque, const avc_pps_t *pps);
    void (*on_sei)(void *opaque, const avc_sei_event_t *sei);
    void (*on_slice)(void *opaque, const avc_slice_header_t *slice);
    void (*on_ref_lists)(void *opaque, const avc_slice_header_t *slice,
                         const avc_ref_list_state_t *lists);
    void (*on_macroblock)(void *opaque, const avc_macroblock_event_t *mb);
    void (*on_mb_pred)(void *opaque, const avc_mb_pred_event_t *pred);
    void (*on_residual)(void *opaque, const avc_residual_event_t *residual);
    void (*on_slice_data)(void *opaque, const avc_slice_data_summary_t *summary);
    void (*on_note)(void *opaque, const char *message);
    void (*on_error)(void *opaque, const char *message, size_t offset);
} avc_parser_callbacks_t;

struct avc_parser {
    avc_parameter_sets_t sets;
    avc_dpb_t dpb;
    uint8_t active_sps_id;
    uint8_t active_sps_valid;
    uint32_t slice_notes_seen;
    avc_parser_callbacks_t callbacks;
    void *opaque;
};

void avc_parser_init(avc_parser_t *parser, avc_parser_callbacks_t callbacks, void *opaque);
int avc_parser_parse_annexb(avc_parser_t *parser, const uint8_t *data, size_t size);

#endif
