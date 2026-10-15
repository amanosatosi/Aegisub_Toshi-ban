#include "motion_track_optimizer.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <set>
#include <stdexcept>
#include <utility>

namespace motion_tracking {
MotionAnalysis OptimizeMotion(std::vector<MotionSample> const& samples,
	std::vector<double> const& tolerance, MotionEncoding encoding) {
	MotionAnalysis result;
	for (double t : tolerance)
		if (!std::isfinite(t) || t <= 0) throw std::invalid_argument("Invalid motion tolerance");
	for (size_t i = 0; i < samples.size(); ++i) {
		if (samples[i].values.size() != tolerance.size() || !std::isfinite(samples[i].time))
			throw std::invalid_argument("Invalid motion sample");
		for (double v : samples[i].values)
			if (!std::isfinite(v)) throw std::invalid_argument("Invalid motion signal");
		if (i && (samples[i].time <= samples[i-1].time || samples[i].frame != samples[i-1].frame + 1))
			throw std::invalid_argument("Motion samples must be consecutive and ordered");
	}
	if (samples.empty()) return result;
	std::set<size_t> knots{0, samples.size()-1};
	if (encoding == MotionEncoding::FrameByFrame) {
		result.frame_by_frame = true;
		for (size_t i = 0; i < samples.size(); ++i) knots.insert(i);
	}
	else {
		// Iterative Douglas-Peucker in media time. Retain actual endpoints:
		// replacing hold endpoints with means breaks relative-tag continuity.
		std::vector<std::pair<size_t,size_t>> pending{{0,samples.size()-1}};
		while (!pending.empty()) {
			auto [a,b] = pending.back(); pending.pop_back();
			double worst = 1;
			size_t split = a;
			for (size_t i = a+1; i < b; ++i) {
				double p = (samples[i].time-samples[a].time)/(samples[b].time-samples[a].time);
				for (size_t k = 0; k < tolerance.size(); ++k) {
					double expected = samples[a].values[k]+p*(samples[b].values[k]-samples[a].values[k]);
					double error = std::abs(samples[i].values[k]-expected)/(samples[i].reference ? 0.0000001 : tolerance[k]);
					if (error > worst) { worst = error; split = i; }
				}
			}
			if (split != a) {
				knots.insert(split);
				pending.emplace_back(a,split); pending.emplace_back(split,b);
			}
		}
		// Difficult paths remain exact per-frame intervals in Automatic. This
		// changes only transform density, never the number of subtitle events.
		if (encoding == MotionEncoding::Automatic && knots.size() > 5 && knots.size()*10 > samples.size()*9) {
			result.frame_by_frame = true;
			for (size_t i = 0; i < samples.size(); ++i) knots.insert(i);
		}
	}
	if (samples.size() == 1)
		result.regions.push_back({0,0,samples[0].values,samples[0].values,true});
	else for (auto it = knots.begin(), next = std::next(it); next != knots.end(); ++it, ++next) {
		auto const& from = samples[*it].values;
		auto const& to = samples[*next].values;
		result.regions.push_back({*it,*next,from,to,from == to});
	}
	return result;
}

std::string MotionAnalysis::Summary() const {
	if (regions.empty()) return "static";
	if (frame_by_frame) return "frame-by-frame transform intervals";
	if (regions.size() == 1) return regions.front().stationary ? "static" : "single linear interval";
	return std::to_string(regions.size()) + " piecewise-linear intervals";
}
}
