#pragma once

#include "ImageToFramebufferDecoder.h"

class BmpToFramebufferConverter final : public ImageToFramebufferDecoder {
 public:
  bool decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer, const RenderConfig& config) override;
  bool getDimensions(const std::string& imagePath, ImageDimensions& dims) const override;
  static bool getDimensionsFromBuffer(const uint8_t* data, size_t size, ImageDimensions& dims);
  const char* getFormatName() const override { return "BMP"; }
};
