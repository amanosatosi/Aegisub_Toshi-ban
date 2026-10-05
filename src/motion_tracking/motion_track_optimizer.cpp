#include "motion_track_optimizer.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <set>
#include <stdexcept>
#include <utility>

namespace motion_tracking {
namespace {
bool Hold(std::vector<MotionSample> const& s, size_t a, size_t b,
	std::vector<double> const& tolerance, std::vector<double>& mean) {
	mean.assign(tolerance.size(), 0);
	for (size_t k = 0; k < tolerance.size(); ++k) {
		double lo = s[a].values[k], hi = lo;
		for (size_t i = a; i <= b; ++i) {
			lo = std::min(lo, s[i].values[k]);
			hi = std::max(hi, s[i].values[k]);
		}
		if (hi - lo > tolerance[k]) return false;
		mean[k] = (lo + hi) / 2;
	}
	return true;
}

// Iterative Douglas-Peucker, measured in rendered units and media time (VFR),
// with mandatory hold endpoints. No recursion proportional to track length.
void Simplify(std::vector<MotionSample> const& s, size_t a, size_t b,
	std::vector<double> const& tolerance, std::set<size_t>& knots) {
	std::vector<std::pair<size_t, size_t>> pending{{a, b}};
	while (!pending.empty()) {
		auto range = pending.back();
		pending.pop_back();
		a = range.first; b = range.second;
		double worst = 1;
		size_t split = a;
		for (size_t i = a + 1; i < b; ++i) {
			double t = (s[i].time - s[a].time) / (s[b].time - s[a].time);
			for (size_t k = 0; k < tolerance.size(); ++k) {
				double expected = s[a].values[k] + t * (s[b].values[k] - s[a].values[k]);
				double error = std::abs(s[i].values[k] - expected) / tolerance[k];
				if (error > worst) { worst = error; split = i; }
			}
		}
		if (split != a) {
			knots.insert(split);
			pending.emplace_back(a, split);
			pending.emplace_back(split, b);
		}
	}
}
}

MotionAnalysis OptimizeMotion(std::vector<MotionSample> const& samples,
	std::vector<double> const& input_tolerance, MotionEncoding encoding) {
	MotionAnalysis result;
	if (samples.empty()) return result;
	auto tolerance = input_tolerance;
	for (double& t : tolerance) {
		if (!std::isfinite(t) || t <= 0) throw std::invalid_argument("Invalid motion tolerance");
		if (encoding == MotionEncoding::ForceOptimized) t *= 1.5;
	}
	for (size_t i = 0; i < samples.size(); ++i) {
		if (samples[i].values.size() != tolerance.size() || !std::isfinite(samples[i].time))
			throw std::invalid_argument("Invalid motion sample");
		for (double v : samples[i].values)
			if (!std::isfinite(v)) throw std::invalid_argument("Invalid motion signal");
		if (i && (samples[i].time <= samples[i-1].time || samples[i].frame != samples[i-1].frame + 1))
			throw std::invalid_argument("Motion samples must be consecutive and ordered");
	}
	auto per_frame = [&] {
		result.regions.clear();
		result.frame_by_frame = true;
		for (size_t i = 0; i < samples.size(); ++i)
			result.regions.push_back({i, i, samples[i].values, samples[i].values, true});
	};
	if (encoding == MotionEncoding::FrameByFrame) { per_frame(); return result; }
	std::vector<double> mean;
	if (Hold(samples, 0, samples.size()-1, tolerance, mean)) {
		result.regions.push_back({0, samples.size()-1, mean, mean, true});
		return result;
	}
	std::set<size_t> knots{0, samples.size()-1};
	// Prefer a single accurate linear segment before looking for local holds.
	// Otherwise a legitimate slow drift could be chopped into tiny "holds".
	Simplify(samples,0,samples.size()-1,tolerance,knots);
	bool single_linear = knots.size() == 2;
	knots = {0,samples.size()-1};
	// A hold has at least three samples and bounded total range; accumulated
	// slow movement cannot masquerade as stationary just because its steps are small.
	for (size_t a = 0; !single_linear && a + 2 < samples.size();) {
		if (!Hold(samples, a, a+2, tolerance, mean)) { ++a; continue; }
		size_t b = a+2;
		while (b+1 < samples.size() && Hold(samples, a, b+1, tolerance, mean)) ++b;
		knots.insert(a); knots.insert(b);
		a = b;
	}
	auto fixed = knots;
	for (auto it = fixed.begin(), next = std::next(it); next != fixed.end(); ++it, ++next)
		Simplify(samples, *it, *next, tolerance, knots);
	for (auto it = knots.begin(), next = std::next(it); next != knots.end(); ++it, ++next) {
		bool hold = Hold(samples, *it, *next, tolerance, mean);
		result.regions.push_back({*it, *next, hold ? mean : samples[*it].values,
			hold ? mean : samples[*next].values, hold});
		// Independently suppress a stationary axis/component even when another
		// signal is moving. All components still share one set of event boundaries.
		auto& region = result.regions.back();
		for (size_t k = 0; k < tolerance.size(); ++k) {
			double lo = samples[*it].values[k], hi = lo;
			for (size_t i = *it; i <= *next; ++i) {
				lo = std::min(lo,samples[i].values[k]); hi = std::max(hi,samples[i].values[k]);
			}
			if (hi-lo <= tolerance[k]) region.from[k] = region.to[k] = (lo+hi)/2;
		}
	}
	// Irregular motion with nearly a knot per frame is clearer and more accurate
	// as exact sampled events. Force optimized retains the bounded piecewise fit.
	if (encoding == MotionEncoding::Automatic && result.regions.size() > 3 &&
		result.regions.size() * 3 > samples.size()) per_frame();
	return result;
}

std::string MotionAnalysis::Summary() const {
	if (regions.empty()) return "no track";
	if (frame_by_frame) return "frame-by-frame required";
	if (regions.size() == 1) return regions.front().stationary ? "static" : "single linear region";
	if (regions.size() > 8) return std::to_string(regions.size()) + " optimized regions";
	std::string s;
	for (auto const& r : regions) {
		if (!s.empty()) s += " -> ";
		s += r.stationary ? "hold" : "linear";
	}
	return s;
}
}
