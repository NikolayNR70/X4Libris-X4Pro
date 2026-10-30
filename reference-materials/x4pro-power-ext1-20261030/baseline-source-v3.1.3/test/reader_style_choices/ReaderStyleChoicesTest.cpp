#include <gtest/gtest.h>

#include "ReaderStyleChoices.h"

CrossPointSettings CrossPointSettings::instance;

TEST(ReaderStyleChoices, MigratesPublisherSentinelsToConcreteDefaults) {
  auto& settings = SETTINGS;
  settings.paragraphAlignment = CrossPointSettings::BOOK_STYLE;
  settings.headingAlignment = 0;
  settings.readerLineHeightChoice = 0;
  settings.readerIndentChoice = 0;
  settings.readerGapChoice = 0;
  settings.readerHeadingSizeChoice = 0;
  settings.readerMarginChoice = 0;

  EXPECT_TRUE(migrateLegacyReaderStyleChoices(settings));
  EXPECT_EQ(CrossPointSettings::JUSTIFIED, settings.paragraphAlignment);
  EXPECT_EQ(1, settings.headingAlignment);  // Center.
  EXPECT_EQ(1, settings.readerLineHeightChoice);
  EXPECT_EQ(1, settings.readerIndentChoice);
  EXPECT_EQ(1, settings.readerGapChoice);
  EXPECT_EQ(2, settings.readerHeadingSizeChoice);
  EXPECT_EQ(2, settings.readerMarginChoice);
  EXPECT_FALSE(migrateLegacyReaderStyleChoices(settings));
}

TEST(ReaderStyleChoices, PreservesExplicitSettings) {
  auto& settings = SETTINGS;
  settings.paragraphAlignment = CrossPointSettings::RIGHT_ALIGN;
  settings.headingAlignment = 4;
  settings.readerLineHeightChoice = 6;
  settings.readerIndentChoice = 5;
  settings.readerGapChoice = 4;
  settings.readerHeadingSizeChoice = 3;
  settings.readerMarginChoice = 7;

  EXPECT_FALSE(migrateLegacyReaderStyleChoices(settings));
  EXPECT_EQ(CrossPointSettings::RIGHT_ALIGN, settings.paragraphAlignment);
  EXPECT_EQ(4, settings.headingAlignment);
  EXPECT_EQ(6, settings.readerLineHeightChoice);
  EXPECT_EQ(5, settings.readerIndentChoice);
  EXPECT_EQ(4, settings.readerGapChoice);
  EXPECT_EQ(3, settings.readerHeadingSizeChoice);
  EXPECT_EQ(7, settings.readerMarginChoice);
}
