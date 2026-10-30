#pragma once

class TtfEpdFont;

class FontCacheManager {
 public:
  void clearCache() { ++clearCount_; }
  void setTtfFont(TtfEpdFont* font) { ttfFont_ = font; }
  TtfEpdFont* ttfFont() const { return ttfFont_; }
  unsigned clearCount() const { return clearCount_; }

 private:
  unsigned clearCount_ = 0;
  TtfEpdFont* ttfFont_ = nullptr;
};
