#include "../../src/mangetsu_gradient_stop_drag.h"
#include <gtest/gtest.h>
#include <vector>

namespace {
struct Stop { double pos; int color; };
}

TEST(MangetsuGradientStopDrag, CrossesNeighborsWithoutDraggingTheirColors) {
    std::vector<Stop> stops{{0, 1}, {25, 2}, {50, 3}, {100, 4}};
    auto selected = mangetsu::MoveGradientStop(stops, 1, 75);
    ASSERT_EQ(2u, selected);
    EXPECT_EQ(1, stops[0].color);
    EXPECT_EQ(3, stops[1].color);
    EXPECT_DOUBLE_EQ(50, stops[1].pos);
    EXPECT_EQ(2, stops[2].color);
    EXPECT_DOUBLE_EQ(75, stops[2].pos);
    EXPECT_EQ(4, stops[3].color);
    selected = mangetsu::MoveGradientStop(stops, selected, 10);
    ASSERT_EQ(1u, selected);
    EXPECT_EQ(2, stops[1].color);
    EXPECT_DOUBLE_EQ(10, stops[1].pos);
    EXPECT_EQ(3, stops[2].color);
    EXPECT_DOUBLE_EQ(50, stops[2].pos);
}

TEST(MangetsuGradientStopDrag, EndpointsStayFixed) {
    std::vector<Stop> stops{{0, 1}, {25, 2}, {50, 3}, {100, 4}};
    EXPECT_EQ(0u, mangetsu::MoveGradientStop(stops, 0, 80));
    EXPECT_EQ(3u, mangetsu::MoveGradientStop(stops, 3, 10));
    EXPECT_EQ(1u, mangetsu::MoveGradientStop(stops, 1, 0));
    EXPECT_EQ(1, stops.front().color);
    EXPECT_EQ(4, stops.back().color);
    EXPECT_EQ(2u, mangetsu::MoveGradientStop(stops, 1, 100));
    EXPECT_EQ(2, stops[2].color);
}

TEST(MangetsuGradientStopDrag, EqualPositionsKeepGrabbedStopIdentity) {
    std::vector<Stop> stops{{0, 1}, {25, 2}, {50, 3}, {75, 4}, {100, 5}};
    auto selected = mangetsu::MoveGradientStop(stops, 1, 50);
    EXPECT_EQ(1u, selected);
    EXPECT_EQ(2, stops[selected].color);
    selected = mangetsu::MoveGradientStop(stops, selected, 75);
    EXPECT_EQ(2u, selected);
    EXPECT_EQ(2, stops[selected].color);
    selected = mangetsu::MoveGradientStop(stops, selected, 76);
    EXPECT_EQ(3u, selected);
    EXPECT_EQ(2, stops[selected].color);
}
