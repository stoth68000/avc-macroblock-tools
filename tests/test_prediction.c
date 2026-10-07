#include "avc/avc_macroblock.h"
#include <assert.h>

static void assert_neighbors(uint32_t width,
                             uint32_t count,
                             uint32_t address,
                             int left_available,
                             uint32_t left,
                             int top_available,
                             uint32_t top,
                             int top_right_available,
                             uint32_t top_right,
                             int top_left_available,
                             uint32_t top_left)
{
    avc_mb_neighbor_addresses_t neighbors;

    assert(avc_mb_neighbor_addresses(count, width, address, &neighbors));
    assert(neighbors.left_available == left_available);
    assert(neighbors.top_available == top_available);
    assert(neighbors.top_right_available == top_right_available);
    assert(neighbors.top_left_available == top_left_available);
    if (left_available) {
        assert(neighbors.left == left);
    }
    if (top_available) {
        assert(neighbors.top == top);
    }
    if (top_right_available) {
        assert(neighbors.top_right == top_right);
    }
    if (top_left_available) {
        assert(neighbors.top_left == top_left);
    }
}

static avc_mb_pred_event_t make_inter_pred(void)
{
    avc_mb_pred_event_t pred = {0};
    pred.kind = AVC_MB_PRED_INTER;
    return pred;
}

static avc_mv_predictor_candidate_t candidate(int available, int16_t x, int16_t y)
{
    avc_mv_predictor_candidate_t c;
    c.available = available;
    c.mv[0] = x;
    c.mv[1] = y;
    return c;
}

static void assert_mvp(unsigned partition,
                       unsigned width,
                       unsigned height,
                       avc_mv_predictor_candidate_t a,
                       avc_mv_predictor_candidate_t b,
                       avc_mv_predictor_candidate_t c,
                       int16_t expected_x,
                       int16_t expected_y)
{
    int16_t mv[2] = {-999, -999};
    avc_mb_predict_mv(partition, width, height, a, b, c, mv);
    assert(mv[0] == expected_x);
    assert(mv[1] == expected_y);
}

static void assert_p_skip_mv(avc_mv_predictor_candidate_t a,
                             avc_mv_predictor_candidate_t b,
                             avc_mv_predictor_candidate_t c,
                             int16_t expected_x,
                             int16_t expected_y)
{
    int16_t mv[2] = {-999, -999};
    avc_mb_derive_p_skip_mv(a, b, c, mv);
    assert(mv[0] == expected_x);
    assert(mv[1] == expected_y);
}

static avc_direct_spatial_candidate_t direct_candidate(int available,
                                                       unsigned list_mask,
                                                       unsigned ref_l0,
                                                       unsigned ref_l1)
{
    avc_direct_spatial_candidate_t c;
    c.available = available;
    c.list_mask = (uint8_t)list_mask;
    c.ref_idx_l0 = ref_l0;
    c.ref_idx_l1 = ref_l1;
    return c;
}

static void assert_direct_ref(avc_direct_spatial_candidate_t a,
                              avc_direct_spatial_candidate_t b,
                              avc_direct_spatial_candidate_t c,
                              unsigned list_bit,
                              int expected_have,
                              unsigned expected_ref)
{
    unsigned ref = 999;
    int have = avc_mb_direct_spatial_ref_idx(a, b, c, list_bit, &ref);
    assert(have == expected_have);
    assert(ref == expected_ref);
}

int main(void)
{
    {
        avc_mb_neighbor_addresses_t neighbors;

        assert(!avc_mb_neighbor_addresses(12, 0, 0, &neighbors));
        assert(!avc_mb_neighbor_addresses(12, 4, 12, &neighbors));
        assert_neighbors(4, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        assert_neighbors(4, 12, 1, 1, 0, 0, 0, 0, 0, 0, 0);
        assert_neighbors(4, 12, 3, 1, 2, 0, 0, 0, 0, 0, 0);
        assert_neighbors(4, 12, 4, 0, 0, 1, 0, 1, 1, 0, 0);
        assert_neighbors(4, 12, 5, 1, 4, 1, 1, 1, 2, 1, 0);
        assert_neighbors(4, 12, 7, 1, 6, 1, 3, 0, 0, 1, 2);
        assert_neighbors(4, 12, 11, 1, 10, 1, 7, 0, 0, 1, 6);
    }

    {
        avc_mb_pred_event_t pred = make_inter_pred();
        unsigned partition;

        pred.partition_count = 1;
        pred.partition_x[0] = 0;
        pred.partition_y[0] = 0;
        pred.partition_width[0] = 16;
        pred.partition_height[0] = 16;
        assert(avc_mb_pred_partition_at(&pred, 0, 0, &partition));
        assert(partition == 0);
        assert(avc_mb_pred_partition_at(&pred, 15, 15, &partition));
        assert(partition == 0);
        assert(!avc_mb_pred_partition_at(&pred, 16, 0, &partition));
    }

    {
        avc_mb_pred_event_t pred = make_inter_pred();
        unsigned partition;

        pred.partition_count = 2;
        pred.partition_x[0] = 0;
        pred.partition_y[0] = 0;
        pred.partition_width[0] = 16;
        pred.partition_height[0] = 8;
        pred.partition_x[1] = 0;
        pred.partition_y[1] = 8;
        pred.partition_width[1] = 16;
        pred.partition_height[1] = 8;
        assert(avc_mb_pred_partition_at(&pred, 15, 7, &partition));
        assert(partition == 0);
        assert(avc_mb_pred_partition_at(&pred, 0, 8, &partition));
        assert(partition == 1);
    }

    {
        avc_mb_pred_event_t pred = make_inter_pred();
        unsigned partition;

        pred.partition_count = 2;
        pred.partition_x[0] = 0;
        pred.partition_y[0] = 0;
        pred.partition_width[0] = 8;
        pred.partition_height[0] = 16;
        pred.partition_x[1] = 8;
        pred.partition_y[1] = 0;
        pred.partition_width[1] = 8;
        pred.partition_height[1] = 16;
        assert(avc_mb_pred_partition_at(&pred, 7, 15, &partition));
        assert(partition == 0);
        assert(avc_mb_pred_partition_at(&pred, 8, 0, &partition));
        assert(partition == 1);
    }

    {
        avc_mb_pred_event_t pred = make_inter_pred();
        unsigned x;
        unsigned y;
        unsigned width;
        unsigned height;

        pred.partition_count = 4;
        pred.partition_x[2] = 0;
        pred.partition_y[2] = 8;
        pred.partition_width[2] = 8;
        pred.partition_height[2] = 8;
        pred.sub_partition_count[2] = 4;
        pred.sub_partition_width[2] = 4;
        pred.sub_partition_height[2] = 4;
        assert(avc_mb_pred_sub_partition_position(&pred, 2, 0, &x, &y, &width, &height));
        assert(x == 0 && y == 8 && width == 4 && height == 4);
        assert(avc_mb_pred_sub_partition_position(&pred, 2, 1, &x, &y, &width, &height));
        assert(x == 4 && y == 8 && width == 4 && height == 4);
        assert(avc_mb_pred_sub_partition_position(&pred, 2, 2, &x, &y, &width, &height));
        assert(x == 0 && y == 12 && width == 4 && height == 4);
        assert(avc_mb_pred_sub_partition_position(&pred, 2, 3, &x, &y, &width, &height));
        assert(x == 4 && y == 12 && width == 4 && height == 4);
        assert(!avc_mb_pred_sub_partition_position(&pred, 2, 4, &x, &y, &width, &height));
    }

    {
        avc_mb_pred_event_t pred = make_inter_pred();
        unsigned x;
        unsigned y;
        unsigned width;
        unsigned height;

        pred.partition_count = 1;
        pred.partition_x[0] = 0;
        pred.partition_y[0] = 0;
        pred.partition_width[0] = 16;
        pred.partition_height[0] = 16;
        assert(avc_mb_pred_sub_partition_position(&pred, 0, 0, &x, &y, &width, &height));
        assert(x == 0 && y == 0 && width == 16 && height == 16);
        assert(!avc_mb_pred_sub_partition_position(&pred, 0, 1, &x, &y, &width, &height));
    }

    {
        assert_mvp(0, 16, 16,
                   candidate(1, 10, 4), candidate(0, 20, 8), candidate(0, 30, 12),
                   10, 4);
        assert_mvp(0, 16, 16,
                   candidate(0, 10, 4), candidate(1, 20, 8), candidate(0, 30, 12),
                   20, 8);
        assert_mvp(0, 16, 16,
                   candidate(0, 10, 4), candidate(0, 20, 8), candidate(1, 30, 12),
                   30, 12);
        assert_mvp(0, 16, 16,
                   candidate(1, 10, 30), candidate(1, 40, 10), candidate(1, 20, 20),
                   20, 20);
        assert_mvp(0, 16, 16,
                   candidate(0, 10, 30), candidate(0, 40, 10), candidate(0, 20, 20),
                   0, 0);
    }

    {
        assert_mvp(0, 16, 8,
                   candidate(1, 10, 4), candidate(1, 20, 8), candidate(1, 30, 12),
                   20, 8);
        assert_mvp(1, 16, 8,
                   candidate(1, 10, 4), candidate(1, 20, 8), candidate(1, 30, 12),
                   10, 4);
        assert_mvp(0, 8, 16,
                   candidate(1, 10, 4), candidate(1, 20, 8), candidate(1, 30, 12),
                   10, 4);
        assert_mvp(1, 8, 16,
                   candidate(1, 10, 4), candidate(1, 20, 8), candidate(1, 30, 12),
                   30, 12);
    }

    {
        avc_mv_predictor_candidate_t a = candidate(0, 10, 4);
        avc_mv_predictor_candidate_t b = candidate(1, 20, 8);
        avc_mv_predictor_candidate_t c = candidate(1, 30, 12);

        assert_mvp(1, 16, 8, a, b, c, 20, 8);
        assert_mvp(0, 8, 16, a, b, c, 20, 8);
    }

    {
        assert_p_skip_mv(candidate(0, 10, 4),
                         candidate(1, 20, 8),
                         candidate(1, 30, 12),
                         0, 0);
        assert_p_skip_mv(candidate(1, 10, 4),
                         candidate(0, 20, 8),
                         candidate(1, 30, 12),
                         0, 0);
        assert_p_skip_mv(candidate(1, 0, 0),
                         candidate(1, 20, 8),
                         candidate(1, 30, 12),
                         0, 0);
        assert_p_skip_mv(candidate(1, 10, 4),
                         candidate(1, 0, 0),
                         candidate(1, 30, 12),
                         0, 0);
        assert_p_skip_mv(candidate(1, 10, 30),
                         candidate(1, 40, 10),
                         candidate(1, 20, 20),
                         20, 20);
        assert_p_skip_mv(candidate(1, 10, 30),
                         candidate(1, 40, 10),
                         candidate(0, 20, 20),
                         10, 10);
    }

    {
        avc_direct_spatial_candidate_t a = direct_candidate(1, 3, 2, 4);
        avc_direct_spatial_candidate_t b = direct_candidate(1, 1, 1, 0);
        avc_direct_spatial_candidate_t c = direct_candidate(1, 2, 0, 3);

        assert_direct_ref(a, b, c, 1, 1, 1);
        assert_direct_ref(a, b, c, 2, 1, 3);
        assert_direct_ref(direct_candidate(0, 3, 0, 0), b, c, 1, 1, 1);
        assert_direct_ref(direct_candidate(0, 3, 0, 0),
                          direct_candidate(1, 2, 7, 2),
                          direct_candidate(0, 1, 1, 8),
                          1, 0, 0);
        assert_direct_ref(direct_candidate(1, 1, 0, 9),
                          direct_candidate(1, 1, 3, 7),
                          direct_candidate(1, 1, 2, 5),
                          2, 0, 0);
        assert_direct_ref(a, b, c, 3, 0, 999);
        assert(!avc_mb_direct_spatial_ref_idx(a, b, c, 1, 0));
    }

    return 0;
}
