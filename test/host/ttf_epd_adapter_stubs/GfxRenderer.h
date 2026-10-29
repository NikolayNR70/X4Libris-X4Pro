#pragma once

#include <EpdFontFamily.h>
#include <FontCacheManager.h>

#include <map>

class GfxRenderer {
 public:
  void replaceFont(int id, const EpdFontFamily& family) { fonts_.insert_or_assign(id, family); }
  void removeFont(int id) { fonts_.erase(id); }
  FontCacheManager* getFontCacheManager() { return &cache_; }
  const std::map<int, EpdFontFamily>& getFontMap() const { return fonts_; }
  size_t registeredFontCount() const { return fonts_.size(); }
  const EpdFontFamily* findFont(int id) const {
    const auto found = fonts_.find(id);
    return found == fonts_.end() ? nullptr : &found->second;
  }

 private:
  std::map<int, EpdFontFamily> fonts_;
  FontCacheManager cache_;
};
