#pragma once

#include <cstdint>
#include <map>
#include <string>

class HalFile {
 public:
  HalFile() = default;
  explicit HalFile(std::string* contents) : contents_(contents) {}

  uint64_t size64() const { return contents_ ? contents_->size() : 0; }
  bool seek64(uint64_t offset) {
    if (!contents_ || offset > contents_->size()) return false;
    position_ = static_cast<size_t>(offset);
    return true;
  }
  size_t write(const uint8_t* data, size_t count) {
    if (!contents_) return 0;
    if (position_ + count > contents_->size()) contents_->resize(position_ + count);
    contents_->replace(position_, count, reinterpret_cast<const char*>(data), count);
    position_ += count;
    return count;
  }
  void flush() {}
  bool close() { return contents_ != nullptr; }

 private:
  std::string* contents_ = nullptr;
  size_t position_ = 0;
};

class HalStorage {
 public:
  bool ready() const { return ready_; }
  bool exists(const char* path) const { return files_.find(path) != files_.end(); }
  bool remove(const char* path) { return files_.erase(path) != 0; }
  bool rename(const char* from, const char* to) {
    const auto it = files_.find(from);
    if (it == files_.end() || files_.find(to) != files_.end()) return false;
    files_[to] = std::move(it->second);
    files_.erase(it);
    return true;
  }
  bool openFileForWrite(const char*, const char* path, HalFile& file) {
    files_[path].clear();
    file = HalFile(&files_[path]);
    return true;
  }
  bool openFileForUpdate(const char*, const char* path, HalFile& file) {
    auto it = files_.find(path);
    if (it == files_.end()) return false;
    file = HalFile(&it->second);
    return true;
  }

  const std::string& contents(const char* path) const { return files_.at(path); }
  void setReady(bool value) { ready_ = value; }
  void clear() { files_.clear(); ready_ = true; }
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }

 private:
  std::map<std::string, std::string> files_;
  bool ready_ = true;
};

#define Storage HalStorage::getInstance()
