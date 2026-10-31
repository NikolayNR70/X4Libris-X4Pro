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

void AlReaderSyncSettings::setWebDavUrl(const std::string& value) {
  if (value.size() <= 256) webDavUrl = value;
}

void AlReaderSyncSettings::setWebDavUsername(const std::string& value) {
  if (value.size() <= 160 && value.find_first_of("\r\n") == std::string::npos) webDavUsername = value;
}

void AlReaderSyncSettings::setWebDavPassword(const std::string& value) {
  if (value.size() <= 64 && value.find_first_of("\r\n") == std::string::npos) webDavPassword = value;
}
