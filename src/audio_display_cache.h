#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>
#include <vector>

namespace audio_display_cache {
using StyleRange = std::pair<int, int>;

// Keep only transitions. The timing controller can emit many adjacent ranges
// with the same style; painting needs only the first position of each style.
inline void AppendStyleTransition(std::vector<StyleRange>& ranges, StyleRange range) {
	if (ranges.empty() || ranges.back().second != range.second)
		ranges.push_back(range);
}

inline std::vector<StyleRange>::const_iterator FirstStyleAt(
	std::vector<StyleRange> const& ranges, int time) {
	auto next = std::upper_bound(ranges.begin(), ranges.end(), time,
		[](int value, StyleRange const& range) { return value < range.first; });
	return next == ranges.begin() ? next : std::prev(next);
}

constexpr size_t particle_limit = 48;
constexpr size_t particles_per_hit = 8;

inline size_t ParticlesToRetire(size_t active) {
	return active + particles_per_hit > particle_limit
		? active + particles_per_hit - particle_limit : 0;
}
}
