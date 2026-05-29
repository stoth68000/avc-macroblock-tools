#include "avc/avc_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void report_error(avc_parser_t *parser, const char *message, size_t offset)
{
    if (parser->callbacks.on_error) {
        parser->callbacks.on_error(parser->opaque, message, offset);
    }
}

static void on_mb_bridge(void *opaque, const avc_macroblock_event_t *mb)
{
    avc_parser_t *parser = (avc_parser_t *)opaque;
    if (parser->callbacks.on_macroblock) {
        parser->callbacks.on_macroblock(parser->opaque, mb);
    }
}

static void on_mb_pred_bridge(void *opaque, const avc_mb_pred_event_t *pred)
{
    avc_parser_t *parser = (avc_parser_t *)opaque;
    if (parser->callbacks.on_mb_pred) {
        parser->callbacks.on_mb_pred(parser->opaque, pred);
    }
}

static void on_residual_bridge(void *opaque, const avc_residual_event_t *residual)
{
    avc_parser_t *parser = (avc_parser_t *)opaque;
    if (parser->callbacks.on_residual) {
        parser->callbacks.on_residual(parser->opaque, residual);
    }
}

static void on_note_bridge(void *opaque, const char *message)
{
    avc_parser_t *parser = (avc_parser_t *)opaque;
    if (parser->callbacks.on_note) {
        parser->callbacks.on_note(parser->opaque, message);
    }
}

void avc_parser_init(avc_parser_t *parser, avc_parser_callbacks_t callbacks, void *opaque)
{
    memset(parser, 0, sizeof(*parser));
    avc_dpb_init(&parser->dpb);
    parser->callbacks = callbacks;
    parser->opaque = opaque;
}

int avc_parser_parse_annexb(avc_parser_t *parser, const uint8_t *data, size_t size)
{
    avc_annexb_reader_t reader;
    avc_nal_unit_t nal;
    int parsed = 0;

    avc_annexb_reader_init(&reader, data, size);
    while (avc_annexb_next(&reader, &nal)) {
        uint8_t *rbsp = NULL;
        size_t rbsp_size;

        parsed++;
        if (parser->callbacks.on_nal) {
            parser->callbacks.on_nal(parser->opaque, &nal);
        }

        rbsp = (uint8_t *)malloc(nal.ebsp_size == 0 ? 1 : nal.ebsp_size);
        if (!rbsp) {
            report_error(parser, "out of memory", nal.offset);
            return 0;
        }
        rbsp_size = avc_ebsp_to_rbsp(nal.ebsp, nal.ebsp_size, rbsp, nal.ebsp_size);
        if (nal.ebsp_size != 0 && rbsp_size == 0) {
            free(rbsp);
            report_error(parser, "failed to convert EBSP to RBSP", nal.offset);
            continue;
        }

        if (nal.header.nal_unit_type == AVC_NAL_SPS) {
            avc_sps_t sps;
            if (avc_parse_sps(rbsp, rbsp_size, &sps)) {
                parser->sets.sps[sps.seq_parameter_set_id] = sps;
                if (parser->callbacks.on_sps) {
                    parser->callbacks.on_sps(parser->opaque, &sps);
                }
            } else {
                report_error(parser, "failed to parse SPS", nal.offset);
            }
        } else if (nal.header.nal_unit_type == AVC_NAL_PPS) {
            avc_pps_t pps;
            if (avc_parse_pps(rbsp, rbsp_size, &pps)) {
                parser->sets.pps[pps.pic_parameter_set_id] = pps;
                if (parser->callbacks.on_pps) {
                    parser->callbacks.on_pps(parser->opaque, &pps);
                }
            } else {
                report_error(parser, "failed to parse PPS", nal.offset);
            }
        } else if (nal.header.nal_unit_type == AVC_NAL_SLICE_NON_IDR || nal.header.nal_unit_type == AVC_NAL_SLICE_IDR) {
            avc_slice_header_t slice;
            if (avc_parse_slice_header(rbsp, rbsp_size, nal.header, &parser->sets, &slice)) {
                avc_ref_list_state_t ref_lists;
                const avc_pps_t *pps = &parser->sets.pps[slice.pic_parameter_set_id];
                const avc_sps_t *sps = &parser->sets.sps[pps->seq_parameter_set_id];

                avc_dpb_build_ref_lists(&parser->dpb, &slice, &ref_lists);
                if (parser->callbacks.on_slice) {
                    parser->callbacks.on_slice(parser->opaque, &slice);
                }
                if (parser->callbacks.on_ref_lists) {
                    parser->callbacks.on_ref_lists(parser->opaque, &slice, &ref_lists);
                }
                if (parser->callbacks.on_macroblock || parser->callbacks.on_residual ||
                    parser->callbacks.on_slice_data || parser->callbacks.on_note) {
                    avc_macroblock_callbacks_t mb_callbacks;
                    avc_slice_data_summary_t summary;

                    mb_callbacks.on_macroblock = on_mb_bridge;
                    mb_callbacks.on_mb_pred = on_mb_pred_bridge;
                    mb_callbacks.on_residual = on_residual_bridge;
                    mb_callbacks.on_note = on_note_bridge;
                    if (avc_parse_slice_data(rbsp, rbsp_size, &slice, &parser->sets,
                                             mb_callbacks, parser, &summary)) {
                        if (parser->callbacks.on_slice_data) {
                            parser->callbacks.on_slice_data(parser->opaque, &summary);
                        }
                    } else {
                        char message[160];
                        snprintf(message, sizeof(message),
                                 "failed to parse slice data: first_mb=%u slice_type=%u entropy=%s parsed_mbs=%u/%u header_bits=%zu",
                                 slice.first_mb_in_slice, slice.slice_type,
                                 summary.entropy_coding_mode_flag ? "CABAC" : "CAVLC",
                                 summary.macroblocks_seen, summary.max_macroblocks,
                                 slice.header_bits);
                        report_error(parser, message, nal.offset);
                    }
                }
                avc_dpb_finish_slice(&parser->dpb, &slice, sps);
            } else {
                report_error(parser, "failed to parse slice header", nal.offset);
            }
        }

        free(rbsp);
    }

    if (!parsed) {
        report_error(parser, "no Annex B NAL units found", 0);
    }
    return parsed > 0;
}
