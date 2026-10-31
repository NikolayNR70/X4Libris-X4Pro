#pragma once

#include <string>

class AlReaderSyncSettings;
namespace JsonSettingsIO {
bool saveAlReader(const AlReaderSyncSettings& settings, const char* path);
bool loadAlReader(AlReaderSyncSettings& settings, const char* json);
}  // namespace JsonSettingsIO

// AlReader.com identifies a reader by a shared free-form string (usually email).
// It is kept in its own X4Libris settings file and never shares KOReader fields.
class AlReaderSyncSettings {
 private:
  static AlReaderSyncSettings instance;
  std::string identifier;
  bool autoSyncOnClose = false;

  AlReaderSyncSettings() = default;
  friend bool JsonSettingsIO::saveAlReader(const AlReaderSyncSettings&, const char*);
  friend bool JsonSettingsIO::loadAlReader(AlReaderSyncSettings&, const char*);

 public:
  AlReaderSyncSettings(const AlReaderSyncSettings&) = delete;
  AlReaderSyncSettings& operator=(const AlReaderSyncSettings&) = delete;

  static AlReaderSyncSettings& getInstance() { return instance; }
  bool saveToFile() const;
  bool loadFromFile();
  void setIdentifier(const std::string& value);
  const std::string& getIdentifier() const { return identifier; }
  bool hasIdentifier() const { return !identifier.empty(); }
  void clearIdentifier();
  bool getAutoSyncOnClose() const { return autoSyncOnClose; }
  void setAutoSyncOnClose(bool enabled) { autoSyncOnClose = enabled; }
};

#define ALREADER_SYNC_SETTINGS AlReaderSyncSettings::getInstance()
