#include "AlReaderSyncSettings.h"

#include <HalStorage.h>
#include <Logging.h>

#include "../../src/JsonSettingsIO.h"

AlReaderSyncSettings AlReaderSyncSettings::instance;

namespace {
constexpr char kSettingsPath[] = "/.x4libris/alreader.json";
}  // namespace

bool AlReaderSyncSettings::saveToFile() const {
  Storage.mkdir("/.x4libris");
  return JsonSettingsIO::saveAlReader(*this, kSettingsPath);
}

bool AlReaderSyncSettings::loadFromFile() {
  if (!Storage.exists(kSettingsPath)) return false;
  const String json = Storage.readFile(kSettingsPath);
  if (json.isEmpty()) return false;
  const bool loaded = JsonSettingsIO::loadAlReader(*this, json.c_str());
  if (!loaded) LOG_ERR("ARS", "Could not read AlReader sync settings");
  return loaded;
}

void AlReaderSyncSettings::setIdentifier(const std::string& value) {
  if (value.size() > 160) return;
  for (const unsigned char c : value) {
    if (c < 0x20 || c == 0x7f) return;
  }
  identifier = value;
}

void AlReaderSyncSettings::clearIdentifier() {
  identifier.clear();
  saveToFile();
}
