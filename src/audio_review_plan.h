// Copyright (c) 2026, JibunSenyou contributors. ISC license.
#pragma once

#include <algorithm>

// A Results preview has its own horizontal coordinate system. Its work is
// bounded by the output width, irrespective of the main audio view's zoom.
struct AudioReviewPlan {
	struct Slice {
		int x = 0;
		int width = 0;
		double begin_ms = 0;
	};
	int begin_ms = 0;
	int end_ms = 0;
	int width = 0;
	static constexpr int columns_per_slice = 256;
	bool Valid() const { return begin_ms >= 0 && end_ms > begin_ms && width > 0; }
	double MillisecondsPerPixel() const {
		return Valid() ? double(end_ms - begin_ms) / width : 0;
	}
	int SliceCount() const { return Valid() ? 1 + (width - 1) / columns_per_slice : 0; }
	Slice GetSlice(int index) const {
		if (index < 0 || index >= SliceCount()) return {};
		int x = index * columns_per_slice;
		return {x, std::min(columns_per_slice, width - x),
			begin_ms + x * MillisecondsPerPixel()};
	}
};
