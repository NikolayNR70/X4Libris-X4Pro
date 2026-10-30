#pragma once

// Selecting an active override again removes it. Keep the serialized v1
// sentinels: -1 inherits the profile; the profile's sentinel restores CSS.
inline int toggledReaderOverride(int current, int selected, bool global, int sourceValue) {
  return current == selected ? (global ? sourceValue : -1) : selected;
}
