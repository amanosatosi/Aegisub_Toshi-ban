#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>

// UI-thread-only, opt-in diagnostics. Set AEGISUB_AUDIO_PERF=1 before starting
// Aegisub; disabled paints only pay for a predictable boolean branch.
class AudioPerf {
public:
	enum Stage {
		Paint, StyleRanges, StyleLookup, Waveform, Spectrum, TileBuild,
		VisibleQuery, DialogueBoundaries, TargetSpans, CapturedBlocks,
		TimelineOverlays, Particles, ParticleUpdate, StageCount
	};
	using Clock = std::chrono::steady_clock;
	struct Sample { double total_ms = 0, max_ms = 0; unsigned count = 0; };
	class Scope {
		Stage stage;
		Clock::time_point start;
		bool running;
	public:
		explicit Scope(Stage stage) : stage(stage), running(AudioPerf::Instance().Enabled()) {
			if (running) start = Clock::now();
		}
		~Scope() { Stop(); }
		void Stop() {
			if (!running) return;
			running = false;
			AudioPerf::Instance().Record(stage,
				std::chrono::duration<double, std::milli>(Clock::now() - start).count());
		}
	};

	static AudioPerf& Instance() { static AudioPerf instance; return instance; }
	bool Enabled() const { return enabled; }
	void Record(Stage stage, double ms) {
		if (!enabled) return;
		auto& s = samples[stage];
		s.total_ms += ms;
		s.max_ms = std::max(s.max_ms, ms);
		++s.count;
	}
	void RefreshRequested() { if (enabled) ++refresh_requests; }
	std::string MaybeSummary() {
		if (!enabled) return {};
		auto now = Clock::now();
		double seconds = std::chrono::duration<double>(now - summary_start).count();
		if (seconds < 5.0) return {};
		std::ostringstream output;
		output << std::fixed << std::setprecision(2)
			<< "Audio paint perf (" << seconds << " s): paints=" << samples[Paint].count
			<< " paints/s=" << samples[Paint].count / seconds
			<< " tracked-39-refresh-requests/s=" << refresh_requests / seconds;
		for (int i = 0; i < StageCount; ++i) {
			auto const& s = samples[i];
			output << " | " << Name(static_cast<Stage>(i)) << " avg="
				<< (s.count ? s.total_ms / s.count : 0.0) << " max=" << s.max_ms
				<< " n=" << s.count;
		}
		samples = {};
		refresh_requests = 0;
		summary_start = now;
		return output.str();
	}
private:
	AudioPerf() : enabled([] {
		auto value = std::getenv("AEGISUB_AUDIO_PERF");
		return value && value[0] == '1';
	}()), summary_start(Clock::now()) { }
	static char const* Name(Stage stage) {
		switch (stage) {
			case Paint: return "paint"; case StyleRanges: return "style-ranges";
			case StyleLookup: return "style-lookup";
			case Waveform: return "waveform"; case Spectrum: return "spectrum";
			case TileBuild: return "tile-build"; case VisibleQuery: return "visible-query";
			case DialogueBoundaries: return "dialogue-boundaries";
			case TargetSpans: return "target-spans";
			case CapturedBlocks: return "captured-blocks";
			case TimelineOverlays: return "timeline-overlays";
			case Particles: return "particles"; case ParticleUpdate: return "particle-update";
			default: return "unknown";
		}
	}
	bool enabled;
	Clock::time_point summary_start;
	std::array<Sample, StageCount> samples{};
	unsigned refresh_requests = 0;
};
