#include <Epub/Epub/converters/BmpToFramebufferConverter.h>
#include <Epub/Epub/converters/ImageDecoderFactory.h>

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

TEST(BmpDecoder, RecognizesAndReadsAStandard24BitBitmap) {
  const auto path = std::filesystem::temp_directory_path() / "wh-bmp-decoder-smoke.bmp";
  // One white 24-bit pixel. BMP rows are padded to four bytes.
  const unsigned char bmp[] = {
      'B', 'M', 58, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0,
      40, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 24, 0,
      0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 0};
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.good());
    out.write(reinterpret_cast<const char*>(bmp), sizeof(bmp));
    ASSERT_TRUE(out.good());
  }

  EXPECT_TRUE(ImageDecoderFactory::isFormatSupported("resource00000.bmp"));
  BmpToFramebufferConverter decoder;
  ImageDimensions dims{};
  ASSERT_TRUE(BmpToFramebufferConverter::getDimensionsFromBuffer(bmp, sizeof(bmp), dims));
  EXPECT_EQ(dims.width, 1);
  EXPECT_EQ(dims.height, 1);
  dims = {};
  ASSERT_TRUE(decoder.getDimensions(path.string(), dims));
  EXPECT_EQ(dims.width, 1);
  EXPECT_EQ(dims.height, 1);
  std::error_code ec;
  std::filesystem::remove(path, ec);
}
