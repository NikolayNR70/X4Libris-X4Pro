#pragma once

#include <string>

#include "NotesCore.h"

class GfxRenderer;

namespace notes {

constexpr const char* kAsleepFile = "/.crosspoint/notes-asleep.txt";

bool readAsleep(AsleepChoice& out);
bool writeAsleep(const AsleepChoice& choice);
void clearAsleep();
// Draws the selected note, read-only, into the current renderer buffer.
bool drawAsleep(GfxRenderer& renderer);

}  // namespace notes
