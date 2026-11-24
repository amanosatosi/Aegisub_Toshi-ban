#pragma once

namespace motion_tracking {
// Keep tracking-layer diagnostics stable; translate at the UI boundary.
constexpr char const* motion_message(char const* message) { return message; }
}
