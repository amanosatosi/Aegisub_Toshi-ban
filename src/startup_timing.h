// Copyright (c) 2026, Aegisub Project. Distributed under the ISC license.
#pragma once

#include <chrono>
#include <libaegisub/log.h>

inline std::chrono::steady_clock::time_point& StartupStarted() {
	static auto started = std::chrono::steady_clock::now();
	return started;
}

inline double StartupMilliseconds(std::chrono::steady_clock::time_point start) {
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

inline void LogStartupTiming(char const *stage) {
	LOG_I("startup/timing") << stage << ": " << StartupMilliseconds(StartupStarted()) << " ms since OnInit";
}
