// Keep the grabbed gradient stop selected when it crosses another stop.
// Endpoints are fixed; only interior stops are reordered.
#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace mangetsu {

// Stop must expose a 'pos' double in percent. Returns the *new* index of the
// same dragged stop; using its old index on the next mouse event moves a
// different color once the positions cross.
template<typename Stop>
size_t MoveGradientStop(std::vector<Stop>& stops, size_t selected, double position) {
    if (stops.size() < 3 || selected == 0 || selected + 1 >= stops.size())
        return selected;

    stops[selected].pos = std::clamp(position, 0.0, 100.0);
    double moved_position = stops[selected].pos;

    // Match the ordering of stable_sort: equal-position stops retain their
    // relative order. Count only interior stops so the fixed endpoint colors
    // can never turn into draggable interior colors, even at 0% or 100%.
    size_t new_index = 1;
    for (size_t i = 1; i + 1 < stops.size(); ++i) {
        if (i == selected)
            continue;
        if (stops[i].pos < moved_position ||
            (stops[i].pos == moved_position && i < selected))
            ++new_index;
    }

    std::stable_sort(stops.begin() + 1, stops.end() - 1,
        [](Stop const& a, Stop const& b) { return a.pos < b.pos; });
    return new_index;
}

} // namespace mangetsu
