#include "avc/avc_dpb.h"
#include <assert.h>

static avc_dpb_picture_t pic(uint32_t frame_num, int32_t poc)
{
    avc_dpb_picture_t p;

    p = (avc_dpb_picture_t){0};
    p.valid = 1;
    p.frame_num = frame_num;
    p.poc = poc;
    p.long_term_frame_idx = 0xffffffffu;
    return p;
}

static avc_dpb_picture_t long_pic(uint32_t frame_idx, int32_t poc)
{
    avc_dpb_picture_t p = pic(frame_idx, poc);
    p.is_long_term = 1;
    p.long_term_frame_idx = frame_idx;
    return p;
}

static void test_p_l0_orders_short_term_desc_then_long_term_asc(void)
{
    avc_dpb_t dpb;
    avc_slice_header_t slice;
    avc_ref_list_state_t lists;

    avc_dpb_init(&dpb);
    dpb.pictures[dpb.count++] = pic(0, 0);
    dpb.pictures[dpb.count++] = pic(3, 6);
    dpb.pictures[dpb.count++] = pic(1, 2);
    dpb.pictures[dpb.count++] = long_pic(4, 30);
    dpb.pictures[dpb.count++] = long_pic(2, 20);

    slice = (avc_slice_header_t){0};
    slice.slice_kind = AVC_SLICE_P;
    slice.num_ref_idx_l0_active_minus1 = 4;

    avc_dpb_build_ref_lists(&dpb, &slice, &lists);

    assert(lists.count_l0 == 5);
    assert(lists.count_l1 == 0);
    assert(!lists.l0[0].is_long_term && lists.l0[0].frame_num == 3);
    assert(!lists.l0[1].is_long_term && lists.l0[1].frame_num == 1);
    assert(!lists.l0[2].is_long_term && lists.l0[2].frame_num == 0);
    assert(lists.l0[3].is_long_term && lists.l0[3].long_term_frame_idx == 2);
    assert(lists.l0[4].is_long_term && lists.l0[4].long_term_frame_idx == 4);
}

static void test_b_lists_are_ordered_around_current_poc(void)
{
    avc_dpb_t dpb;
    avc_slice_header_t slice;
    avc_ref_list_state_t lists;

    avc_dpb_init(&dpb);
    dpb.pictures[dpb.count++] = pic(0, 0);
    dpb.pictures[dpb.count++] = pic(2, 8);
    dpb.pictures[dpb.count++] = pic(4, 16);

    slice = (avc_slice_header_t){0};
    slice.slice_kind = AVC_SLICE_B;
    slice.pic_order_cnt_lsb = 10;
    slice.num_ref_idx_l0_active_minus1 = 2;
    slice.num_ref_idx_l1_active_minus1 = 2;

    avc_dpb_build_ref_lists(&dpb, &slice, &lists);

    assert(lists.count_l0 == 3);
    assert(lists.count_l1 == 3);
    assert(lists.l0[0].poc == 8);
    assert(lists.l0[1].poc == 0);
    assert(lists.l0[2].poc == 16);
    assert(lists.l1[0].poc == 16);
    assert(lists.l1[1].poc == 8);
    assert(lists.l1[2].poc == 0);
}

static void test_b_equal_lists_swap_first_two_l1_entries(void)
{
    avc_dpb_t dpb;
    avc_slice_header_t slice;
    avc_ref_list_state_t lists;

    avc_dpb_init(&dpb);
    dpb.pictures[dpb.count++] = pic(0, 0);
    dpb.pictures[dpb.count++] = pic(2, 8);

    slice = (avc_slice_header_t){0};
    slice.slice_kind = AVC_SLICE_B;
    slice.pic_order_cnt_lsb = 20;
    slice.num_ref_idx_l0_active_minus1 = 1;
    slice.num_ref_idx_l1_active_minus1 = 1;

    avc_dpb_build_ref_lists(&dpb, &slice, &lists);

    assert(lists.count_l0 == 2);
    assert(lists.count_l1 == 2);
    assert(lists.l0[0].poc == 8);
    assert(lists.l0[1].poc == 0);
    assert(lists.l1[0].poc == 0);
    assert(lists.l1[1].poc == 8);
}

static void test_ref_pic_list_modification_moves_short_and_long_refs(void)
{
    avc_dpb_t dpb;
    avc_slice_header_t slice;
    avc_ref_list_state_t lists;

    avc_dpb_init(&dpb);
    dpb.pictures[dpb.count++] = pic(0, 0);
    dpb.pictures[dpb.count++] = pic(1, 2);
    dpb.pictures[dpb.count++] = pic(2, 4);
    dpb.pictures[dpb.count++] = long_pic(0, 30);
    dpb.pictures[dpb.count++] = long_pic(1, 40);

    slice = (avc_slice_header_t){0};
    slice.slice_kind = AVC_SLICE_P;
    slice.frame_num = 3;
    slice.num_ref_idx_l0_active_minus1 = 4;
    slice.ref_pic_list_modification_flag_l0 = 1;
    slice.ref_pic_list_modification_count_l0 = 2;
    slice.ref_pic_list_modifications_l0[0].modification_of_pic_nums_idc = 0;
    slice.ref_pic_list_modifications_l0[0].abs_diff_pic_num_minus1 = 2;
    slice.ref_pic_list_modifications_l0[1].modification_of_pic_nums_idc = 2;
    slice.ref_pic_list_modifications_l0[1].long_term_pic_num = 1;

    avc_dpb_build_ref_lists(&dpb, &slice, &lists);

    assert(lists.count_l0 == 5);
    assert(!lists.l0[0].is_long_term && lists.l0[0].frame_num == 0);
    assert(lists.l0[1].is_long_term && lists.l0[1].long_term_frame_idx == 1);
    assert(!lists.l0[2].is_long_term && lists.l0[2].frame_num == 2);
    assert(!lists.l0[3].is_long_term && lists.l0[3].frame_num == 1);
    assert(lists.l0[4].is_long_term && lists.l0[4].long_term_frame_idx == 0);
}

static void test_active_ref_counts_limit_lists(void)
{
    avc_dpb_t dpb;
    avc_slice_header_t slice;
    avc_ref_list_state_t lists;

    avc_dpb_init(&dpb);
    dpb.pictures[dpb.count++] = pic(0, 0);
    dpb.pictures[dpb.count++] = pic(1, 2);
    dpb.pictures[dpb.count++] = pic(2, 4);

    slice = (avc_slice_header_t){0};
    slice.slice_kind = AVC_SLICE_P;
    slice.num_ref_idx_l0_active_minus1 = 0;

    avc_dpb_build_ref_lists(&dpb, &slice, &lists);

    assert(lists.count_l0 == 1);
    assert(lists.l0[0].frame_num == 2);
    assert(lists.count_l1 == 0);
}

static void test_b_active_ref_counts_limit_both_lists_after_modification(void)
{
    avc_dpb_t dpb;
    avc_slice_header_t slice;
    avc_ref_list_state_t lists;

    avc_dpb_init(&dpb);
    dpb.pictures[dpb.count++] = pic(0, 0);
    dpb.pictures[dpb.count++] = pic(2, 8);
    dpb.pictures[dpb.count++] = pic(4, 16);
    dpb.pictures[dpb.count++] = pic(6, 24);

    slice = (avc_slice_header_t){0};
    slice.slice_kind = AVC_SLICE_B;
    slice.frame_num = 7;
    slice.pic_order_cnt_lsb = 10;
    slice.num_ref_idx_l0_active_minus1 = 1;
    slice.num_ref_idx_l1_active_minus1 = 0;
    slice.ref_pic_list_modification_flag_l0 = 1;
    slice.ref_pic_list_modification_count_l0 = 1;
    slice.ref_pic_list_modifications_l0[0].modification_of_pic_nums_idc = 0;
    slice.ref_pic_list_modifications_l0[0].abs_diff_pic_num_minus1 = 2;

    avc_dpb_build_ref_lists(&dpb, &slice, &lists);

    assert(lists.count_l0 == 2);
    assert(lists.count_l1 == 1);
    assert(lists.l0[0].frame_num == 4);
    assert(lists.l0[1].poc == 8);
    assert(lists.l1[0].poc == 16);
}

int main(void)
{
    test_p_l0_orders_short_term_desc_then_long_term_asc();
    test_b_lists_are_ordered_around_current_poc();
    test_b_equal_lists_swap_first_two_l1_entries();
    test_ref_pic_list_modification_moves_short_and_long_refs();
    test_active_ref_counts_limit_lists();
    test_b_active_ref_counts_limit_both_lists_after_modification();
    return 0;
}
