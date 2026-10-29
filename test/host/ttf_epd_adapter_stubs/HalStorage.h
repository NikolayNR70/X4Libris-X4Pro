#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

class FsFile {
 public:
  FsFile() = default;
  explicit FsFile(std::filesystem::path path) : path_(std::move(path)), valid_(std::filesystem::exists(path_)) {
    if (valid_ && std::filesystem::is_directory(path_)) {
      for (const auto& entry : std::filesystem::directory_iterator(path_)) entries_.push_back(entry.path());
      std::stable_sort(entries_.begin(), entries_.end(), [](const auto& a, const auto& b) {
        return std::filesystem::is_directory(a) < std::filesystem::is_directory(b);
      });
    }
  }

  explicit operator bool() const { return valid_; }
  bool isDirectory() const { return valid_ && std::filesystem::is_directory(path_); }
  FsFile openNextFile() {
    if (!isDirectory() || next_ >= entries_.size()) return {};
    return FsFile(entries_[next_++]);
  }
  size_t getName(char* out, size_t capacity) const {
    if (!out || capacity == 0 || !valid_) return 0;
    const std::string name = path_.filename().string();
    const size_t copied = std::min(name.size(), capacity - 1);
    std::memcpy(out, name.data(), copied);
    out[copied] = '\0';
    return copied;
  }
  uint64_t fileSize64() const {
    return valid_ && !isDirectory() ? std::filesystem::file_size(path_) : 0;
  }
  uint64_t fileSize() const { return fileSize64(); }
  bool seekSet64(uint64_t offset) {
    if (!valid_ || isDirectory() || offset > fileSize64()) return false;
    stream_.close();
    stream_.clear();
    stream_.open(path_, std::ios::binary);
    if (!stream_) return false;
    stream_.seekg(static_cast<std::streamoff>(offset));
    return static_cast<bool>(stream_);
  }
  int read(void* out, unsigned long count) {
    if (!stream_ || !out) return -1;
    stream_.read(static_cast<char*>(out), static_cast<std::streamsize>(count));
    return static_cast<int>(stream_.gcount());
  }
  bool close() {
    if (stream_.is_open()) stream_.close();
    return true;
  }

 private:
  std::filesystem::path path_;
  std::vector<std::filesystem::path> entries_;
  size_t next_ = 0;
  bool valid_ = false;
  std::ifstream stream_;
};

class HalFile {
 public:
  bool open(const std::filesystem::path& path) {
    path_ = path;
    stream_.open(path_, std::ios::binary);
    return static_cast<bool>(stream_);
  }
  explicit operator bool() const { return stream_.is_open(); }
  uint64_t fileSize64() const { return std::filesystem::file_size(path_); }
  bool seekSet64(uint64_t offset) {
    if (!stream_.is_open() || offset > fileSize64()) return false;
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(offset));
    return static_cast<bool>(stream_);
  }
  int read(void* out, size_t count) {
    if (!stream_.is_open() || !out) return -1;
    stream_.read(static_cast<char*>(out), static_cast<std::streamsize>(count));
    return static_cast<int>(stream_.gcount());
  }

 private:
  std::filesystem::path path_;
  std::ifstream stream_;
};

class HalStorage {
 public:
  static void setTestRoot(std::filesystem::path root) { root_ = std::move(root); }
  FsFile open(const char* path, int = 0) {
    const auto physical = resolvePath(path);
    return physical.empty() ? FsFile{} : FsFile(physical);
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    const auto physical = resolvePath(path);
    return !physical.empty() && file.open(physical);
  }
  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }

 private:
  static std::filesystem::path resolvePath(const char* path) {
    if (!path || root_.empty()) return {};
    const std::string logical(path);
    const char* subPath = nullptr;
    std::filesystem::path base;
    if (logical == "/.crosspoint/fonts" || logical.rfind("/.crosspoint/fonts/", 0) == 0) {
      base = root_ / "primary";
      subPath = logical.size() > 18 ? logical.c_str() + 18 : nullptr;
    } else if (logical == "/.fonts" || logical.rfind("/.fonts/", 0) == 0) {
      base = root_ / "hidden";
      subPath = logical.size() > 7 ? logical.c_str() + 7 : nullptr;
    } else if (logical == "/fonts" || logical.rfind("/fonts/", 0) == 0) {
      base = root_ / "visible";
      subPath = logical.size() > 6 ? logical.c_str() + 6 : nullptr;
    } else {
      return {};
    }
    if (subPath && *subPath) base /= std::filesystem::path(subPath + 1);
    return base;
  }
  inline static std::filesystem::path root_;
};

#define Storage HalStorage::getInstance()
