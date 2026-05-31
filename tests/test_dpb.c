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

int main(void)
{
    test_b_lists_are_ordered_around_current_poc();
    test_active_ref_counts_limit_lists();
    return 0;
}
