#pragma once
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <limits>

#include "Arduino.h"
#include "Print.h"
namespace fs = std::filesystem;
inline fs::path physical(const std::string& p) {
  return fs::path(std::getenv("FB2_TEST_SD")) / p.substr(p[0] == '/' ? 1 : 0);
}
struct HalFile {
  std::fstream f;
  bool writing = false;
  bool isOpen() const { return f.is_open(); }
  HalFile() = default;
  HalFile(HalFile&&) = default;
  HalFile& operator=(HalFile&&) = default;
  bool open(const std::string& p, bool w) {
    writing = w;
    f.open(physical(p), std::ios::binary | (w ? (std::ios::out | std::ios::trunc) : std::ios::in));
    return f.is_open();
  }
  size_t write(const void* p, size_t n) {
    if (std::getenv("FB2_TEST_FAIL_WRITE")) return 0;
    f.write(static_cast<const char*>(p), n);
    return f ? n : 0;
  }
  int read(void* p, size_t n) {
    f.read(static_cast<char*>(p), n);
    return static_cast<int>(f.gcount());
  }
  size_t position() { return static_cast<size_t>(writing ? f.tellp() : f.tellg()); }
  size_t size() {
    auto p = f.tellg();
    f.seekg(0, std::ios::end);
    auto n = f.tellg();
    f.seekg(p);
    return static_cast<size_t>(n);
  }
  bool seek(size_t p) {
    f.clear();
    f.seekg(p);
    return !!f;
  }
  bool close() {
    if (!f.is_open()) return true;
    const bool ok = !f.bad();
    f.close();
    return ok;
  }
  bool seekCur(int64_t offset) {
    f.clear();
    f.seekg(offset, std::ios::cur);
    return !!f;
  }
  int available() {
    if (!f.is_open()) return 0;
    const auto old = f.tellg();
    f.seekg(0, std::ios::end);
    const auto end = f.tellg();
    f.seekg(old);
    if (old < 0 || end < old) return 0;
    const auto remain = end - old;
    return remain > std::numeric_limits<int>::max() ? std::numeric_limits<int>::max() : static_cast<int>(remain);
  }
  explicit operator bool() const { return f.is_open(); }
};
using FsFile = HalFile;
struct TestStorage {
  bool openFileForRead(const char*, const std::string& p, HalFile& f) {
    if ((std::getenv("FB2_TEST_CORRUPT_STAGE") && p.find("book.epub.part") != std::string::npos) ||
        (std::getenv("FB2_TEST_CORRUPT_FINAL") && p.size() >= 5 && p.substr(p.size() - 5) == ".epub")) {
      std::fstream corrupt(physical(p), std::ios::binary | std::ios::in | std::ios::out);
      corrupt.seekp(40);
      corrupt.put('!');
    }
    return f.open(p, false);
  }
  bool openFileForWrite(const char*, const std::string& p, HalFile& f) { return f.open(p, true); }
  bool exists(const char* p) { return fs::exists(physical(p)); }
  bool mkdir(const char* p) { return fs::create_directories(physical(p)) || fs::is_directory(physical(p)); }
  bool ensureDirectoryExists(const char* p) {
    return fs::create_directories(physical(p)) || fs::is_directory(physical(p));
  }
  bool removeDir(const char* p) {
    fs::remove_all(physical(p));
    return true;
  }
  bool remove(const char* p) { return fs::remove(physical(p)); }
  bool renameNoReplace(const char* a, const char* b) {
    if (std::getenv("FB2_TEST_RACE") && std::string(b).size() >= 5 &&
        std::string(b).substr(std::string(b).size() - 5) == ".epub") {
      std::ofstream(physical(b)) << "racing upload";
    }
    if (exists(b) || std::getenv("FB2_TEST_FAIL_RENAME")) return false;
    if (std::getenv("FB2_TEST_FAIL_HIDE") && std::string(b).find("/.") != std::string::npos) return false;
    std::error_code e;
    fs::rename(physical(a), physical(b), e);
    if (!e && std::getenv("FB2_TEST_SOURCE_CHANGED") && std::string(b).size() >= 5 &&
        std::string(b).substr(std::string(b).size() - 5) == ".epub") {
      auto source = std::string(b).substr(0, std::string(b).size() - 5) + ".fb2";
      std::ofstream(physical(source), std::ios::app) << "changed source";
    }
    return !e;
  }
  bool writeFile(const char* p, const String& s) {
    std::ofstream f(physical(p), std::ios::binary);
    f << s;
    return !!f;
  }
  String readFile(const char* p) {
    std::ifstream f(physical(p), std::ios::binary);
    return String(std::string(std::istreambuf_iterator<char>(f), {}));
  }
};
inline TestStorage Storage;
