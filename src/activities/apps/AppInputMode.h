#pragma once

namespace app_input {

// Use button-target navigation when touch hardware is absent or the user has
// disabled touch events on a device that has a digitizer.
constexpr bool useButtonNavigation(const bool hasTouchHardware, const bool touchEventsEnabled) {
  return !hasTouchHardware || !touchEventsEnabled;
}

}  // namespace app_input
