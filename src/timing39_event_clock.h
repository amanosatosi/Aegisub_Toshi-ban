// Copyright (c) 2026, JibunSenyou contributors. ISC license.
#pragma once
#include <cstdint>
#include <wx/event.h>

#ifdef __WXMSW__
#include <windows.h>
#endif

namespace agi { namespace timing39 {
// wxMSW copies GetMessageTime() into key events. Other wx ports have not been
// verified to retain a pre-dispatch timestamp, so do not claim lag immunity.
inline bool InputEventTick(wxKeyEvent const& event, uint32_t& tick) {
#ifdef __WXMSW__
	if (!event.GetTimestamp()) return false;
	tick = uint32_t(event.GetTimestamp());
	return true;
#else
	(void)event; (void)tick;
	return false;
#endif
}
inline uint32_t InputClockNow() {
#ifdef __WXMSW__
	return uint32_t(::GetTickCount64());
#else
	return 0;
#endif
}
} }
