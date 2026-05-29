#include "avc/avc_bitreader.h"
#include "avc/avc_syntax.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

static size_t pack_bits(const char *bits, uint8_t *out, size_t capacity)
{
    size_t i;
    size_t bit_count = strlen(bits);

    memset(out, 0, capacity);
    assert(((bit_count + 7u) / 8u) <= capacity);
    for (i = 0; i < bit_count; i++) {
        if (bits[i] == '1') {
            out[i >> 3] |= (uint8_t)(1u << (7u - (i & 7u)));
        } else {
            assert(bits[i] == '0');
        }
    }
    return (bit_count + 7u) / 8u;
}

int main(void)
{
    {
        const uint8_t bits[] = {0xb6, 0xc0};
        avc_bitreader_t br;

        avc_br_init(&br, bits, sizeof(bits));
        assert(avc_br_read_bits(&br, 3) == 5);
        assert(avc_br_read_bit(&br) == 1);
        assert(avc_br_read_bits(&br, 4) == 6);
        assert(avc_br_read_ue(&br) == 0);
        assert(avc_br_consume_rbsp_trailing_bits(&br));
        assert(!br.error);
    }

    {
        uint8_t rbsp[16];
        size_t rbsp_size;
        avc_parameter_sets_t sets = {0};
        avc_nal_header_t nal = {0};
        avc_slice_header_t slice;

        sets.sps[0].present = 1;
        sets.sps[0].chroma_format_idc = 1;
        sets.sps[0].log2_max_frame_num_minus4 = 0;
        sets.sps[0].pic_order_cnt_type = 2;
        sets.sps[0].frame_mbs_only_flag = 1;
        sets.pps[0].present = 1;
        sets.pps[0].weighted_pred_flag = 1;

        nal.nal_ref_idc = 1;
        nal.nal_unit_type = AVC_NAL_SLICE_NON_IDR;

        rbsp_size = pack_bits("111000001110010011101010101010011011", rbsp, sizeof(rbsp));
        assert(avc_parse_slice_header(rbsp, rbsp_size, nal, &sets, &slice));
        assert(slice.slice_kind == AVC_SLICE_P);
        assert(slice.ref_pic_list_modification_flag_l0 == 1);
        assert(slice.ref_pic_list_modification_count_l0 == 1);
        assert(slice.ref_pic_list_modifications_l0[0].modification_of_pic_nums_idc == 0);
        assert(slice.ref_pic_list_modifications_l0[0].abs_diff_pic_num_minus1 == 0);
        assert(slice.pred_weight_table_present == 1);
        assert(slice.luma_log2_weight_denom == 0);
        assert(slice.chroma_log2_weight_denom == 0);
        assert(slice.pred_weight_l0[0].luma_weight_flag == 1);
        assert(slice.pred_weight_l0[0].luma_weight == 1);
        assert(slice.pred_weight_l0[0].luma_offset == 0);
        assert(slice.pred_weight_l0[0].chroma_weight[0] == 1);
        assert(slice.pred_weight_l0[0].chroma_weight[1] == 1);
        assert(slice.adaptive_ref_pic_marking_mode_flag == 1);
        assert(slice.dec_ref_pic_marking_count == 2);
        assert(slice.dec_ref_pic_marking[0].memory_management_control_operation == 1);
        assert(slice.dec_ref_pic_marking[0].difference_of_pic_nums_minus1 == 0);
        assert(slice.dec_ref_pic_marking[1].memory_management_control_operation == 5);
        assert(slice.slice_qp_delta == 0);
    }

    return 0;
}
