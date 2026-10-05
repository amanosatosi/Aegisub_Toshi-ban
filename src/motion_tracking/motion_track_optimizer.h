#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace motion_tracking {

enum class MotionEncoding { Automatic, ForceOptimized, FrameByFrame };

// Script pixels, percentage points, and ASS degrees respectively. Every
// signal is checked independently; disabling an axis removes it from analysis.
struct MotionTolerances {
	double position = 0.35;
	double scale = 0.20;
	double rotation = 0.08;
	double outline = 0.05;
};

struct MotionSample {
	int frame = 0;
	double time = 0;
	std::vector<double> values;
};

struct MotionRegion {
	size_t first = 0;
	size_t last = 0; // shared interpolation knot; the following region owns it
	std::vector<double> from;
	std::vector<double> to;
	bool stationary = false;
};

struct MotionAnalysis {
	std::vector<MotionRegion> regions;
	bool frame_by_frame = false;
	std::string Summary() const;
};

MotionAnalysis OptimizeMotion(std::vector<MotionSample> const& samples,
	std::vector<double> const& tolerances, MotionEncoding encoding);

}
