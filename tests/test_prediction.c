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

    return 0;
}
