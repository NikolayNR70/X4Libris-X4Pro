#include "BmpToFramebufferConverter.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>

#include <limits>

namespace {
bool openBitmap(const std::string& imagePath, FsFile& file, Bitmap& bitmap, ImageDimensions* dims) {
  if (!Storage.openFileForRead("BMP", imagePath, file)) {
    LOG_DBG("BMP", "Could not open image: %s", imagePath.c_str());
    return false;
  }
  const BmpReaderError result = bitmap.parseHeaders();
  if (result != BmpReaderError::Ok || !bitmap.isComplete()) {
    LOG_DBG("BMP", "Unsupported or incomplete image (%s): %s", Bitmap::errorToString(result), imagePath.c_str());
    file.close();
    return false;
  }
  if (dims) {
    if (bitmap.getWidth() > std::numeric_limits<int16_t>::max() ||
        bitmap.getHeight() > std::numeric_limits<int16_t>::max()) {
      file.close();
      return false;
    }
    dims->width = static_cast<int16_t>(bitmap.getWidth());
    dims->height = static_cast<int16_t>(bitmap.getHeight());
  }
  return true;
}
}  // namespace

bool BmpToFramebufferConverter::getDimensions(const std::string& imagePath, ImageDimensions& dims) const {
  FsFile file;
  Bitmap bitmap(file);
  return openBitmap(imagePath, file, bitmap, &dims);
}

bool BmpToFramebufferConverter::getDimensionsFromBuffer(const uint8_t* data, const size_t size,
                                                          ImageDimensions& dims) {
  // BITMAPINFOHEADER and later use signed 32-bit dimensions at offsets 18 and 22. The
  // direct-source path only needs these fields to lay out an image; Bitmap::parseHeaders()
  // performs the complete format/compression/pixel-data validation before rendering.
  if (!data || size < 26 || data[0] != 'B' || data[1] != 'M') return false;
  const uint32_t dibSize = static_cast<uint32_t>(data[14]) | (static_cast<uint32_t>(data[15]) << 8) |
                           (static_cast<uint32_t>(data[16]) << 16) | (static_cast<uint32_t>(data[17]) << 24);
  if (dibSize < 40) return false;
  const uint32_t rawWidth = static_cast<uint32_t>(data[18]) | (static_cast<uint32_t>(data[19]) << 8) |
                            (static_cast<uint32_t>(data[20]) << 16) | (static_cast<uint32_t>(data[21]) << 24);
  const uint32_t rawHeight = static_cast<uint32_t>(data[22]) | (static_cast<uint32_t>(data[23]) << 8) |
                             (static_cast<uint32_t>(data[24]) << 16) | (static_cast<uint32_t>(data[25]) << 24);
  const int32_t width = static_cast<int32_t>(rawWidth);
  const int32_t signedHeight = static_cast<int32_t>(rawHeight);
  if (width <= 0 || signedHeight == 0 || signedHeight == INT32_MIN || width > INT16_MAX ||
      (signedHeight < 0 ? -signedHeight : signedHeight) > INT16_MAX) {
    return false;
  }
  dims.width = static_cast<int16_t>(width);
  dims.height = static_cast<int16_t>(signedHeight < 0 ? -signedHeight : signedHeight);
  return true;
}

bool BmpToFramebufferConverter::decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                                                     const RenderConfig& config) {
  FsFile file;
  Bitmap bitmap(file, config.useDithering);
  if (!openBitmap(imagePath, file, bitmap, nullptr)) return false;
  renderer.drawBitmap(bitmap, config.x, config.y, config.maxWidth, config.maxHeight);
  file.close();
  return true;
}
