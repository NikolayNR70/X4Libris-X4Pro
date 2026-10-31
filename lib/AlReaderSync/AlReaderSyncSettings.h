#pragma once

#include <string>

class AlReaderSyncSettings;
namespace JsonSettingsIO {
bool saveAlReader(const AlReaderSyncSettings& settings, const char* path);
bool loadAlReader(AlReaderSyncSettings& settings, const char* json);
}  // namespace JsonSettingsIO

// AlReader's independent WebDAV configuration; never shares KOReader fields.
class AlReaderSyncSettings {
 private:
  static AlReaderSyncSettings instance;
  std::string webDavUrl;
  std::string webDavUsername;
  std::string webDavPassword;
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
  void setWebDavUrl(const std::string& value);
  void setWebDavUsername(const std::string& value);
  void setWebDavPassword(const std::string& value);
  const std::string& getWebDavUrl() const { return webDavUrl; }
  const std::string& getWebDavUsername() const { return webDavUsername; }
  const std::string& getWebDavPassword() const { return webDavPassword; }
  bool hasWebDavCredentials() const { return !webDavUrl.empty() && !webDavUsername.empty() && !webDavPassword.empty(); }
  bool getAutoSyncOnClose() const { return autoSyncOnClose; }
  void setAutoSyncOnClose(bool enabled) { autoSyncOnClose = enabled; }
};

#define ALREADER_SYNC_SETTINGS AlReaderSyncSettings::getInstance()
