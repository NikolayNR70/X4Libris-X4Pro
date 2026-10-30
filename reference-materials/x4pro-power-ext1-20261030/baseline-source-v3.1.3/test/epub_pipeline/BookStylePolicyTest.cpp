#include <gtest/gtest.h>
#include "src/ReaderStylePolicy.h"
#include "Epub/Section.h"
#include "Epub/ParsedText.h"
#include "GfxRenderer.h"
#include "src/ReaderFormatting.h"
#include "src/BootTrialHealth.h"
#include "src/ReaderOverrideToggle.h"
TEST(BookStylePolicy, ActiveOverrideTogglesOffWithoutChangingLegacySentinels) {
  for(int source:{0,4}) {
    for(int value=1;value<=11;++value) {
      if(value==source) continue;
      EXPECT_EQ(toggledReaderOverride(source,value,true,source),value);
      EXPECT_EQ(toggledReaderOverride(value,value,true,source),source);
      EXPECT_EQ(toggledReaderOverride(-1,value,false,source),value);
      EXPECT_EQ(toggledReaderOverride(value,value,false,source),-1);
    }
  }
  EXPECT_EQ(toggledReaderOverride(4,0,true,4),0); // explicit justification
  EXPECT_EQ(toggledReaderOverride(0,0,true,4),4); // undo restores publisher
  EXPECT_EQ(toggledReaderOverride(0,0,false,4),-1);
  EXPECT_EQ(ReaderFormatting::choice(-1,0),0);
  EXPECT_EQ(ReaderFormatting::choice(0,3),0); // old per-book publisher setting
}
TEST(BookStylePolicy, EveryOptionWorksWithoutMasterAndCssIsAlwaysLoaded) {
  for(bool master:{false,true}) {
    ReaderStylePolicy p{master};
    for(int8_t book:{-1,0,1}) EXPECT_TRUE(p.embedded(book,0));
    EXPECT_TRUE(p.option(-1,1));EXPECT_FALSE(p.option(0,1));
    EXPECT_TRUE(p.option(1,0));EXPECT_EQ(p.alignment(-1,4),4);
    EXPECT_EQ(p.alignment(2,4),2);EXPECT_EQ(p.images(-1,2),2);
  }
}
TEST(BookStylePolicy, EachPropertyDistinguishesGlobalBookAndExplicitZero) {
  EXPECT_EQ(ReaderFormatting::choice(-1,3),3);
  EXPECT_EQ(ReaderFormatting::choice(0,3),0);
  EXPECT_EQ(ReaderFormatting::choice(1,3),1);
  EXPECT_EQ(ReaderFormatting::indentPercent(0),-1);
  EXPECT_EQ(ReaderFormatting::indentPercent(1),0);
  EXPECT_EQ(ReaderFormatting::gapPercent(0),-1);
  EXPECT_EQ(ReaderFormatting::gapPercent(1),0);
  EXPECT_EQ(ReaderFormatting::linePercent(0),-1);
  EXPECT_EQ(ReaderFormatting::linePercent(11),200);
}
TEST(BootTrialHealth, NeedsRenderedResponsiveUiForThirtySeconds) {
  BootTrialHealth h;
  for(unsigned t=0;t<60000;t+=100)EXPECT_FALSE(h.observe(t,false));
  for(unsigned t=60000;t<90000;t+=100)EXPECT_FALSE(h.observe(t,true));
  EXPECT_TRUE(h.observe(90000,true));
}
TEST(BootTrialHealth, BlockedLoopOrRenderRestartsObservationAndHandlesWrap) {
  BootTrialHealth h;
  for(unsigned t=0;t<29000;t+=100)EXPECT_FALSE(h.observe(t,true));
  EXPECT_FALSE(h.observe(32000,true));
  EXPECT_FALSE(h.observe(60000,false));
  for(unsigned t=60100;t<90100;t+=100)EXPECT_FALSE(h.observe(t,true));
  EXPECT_TRUE(h.observe(90100,true));
  BootTrialHealth wrap;uint32_t start=UINT32_MAX-20000;
  for(unsigned t=0;t<30000;t+=100)EXPECT_FALSE(wrap.observe(start+t,true));
  EXPECT_TRUE(wrap.observe(start+30000,true));
}
TEST(BootTrialHealth, BriefRedrawsDoNotPreventAutomaticAcceptance) {
  BootTrialHealth h;
  for(unsigned t=0;t<30000;t+=100)EXPECT_FALSE(h.observe(t,t%2000<1500));
  EXPECT_TRUE(h.observe(30000,true));
}
TEST(BootTrialHealth, ContinuousRenderStallRestartsObservation) {
  BootTrialHealth h;
  for(unsigned t=0;t<29000;t+=100)EXPECT_FALSE(h.observe(t,true));
  for(unsigned t=29000;t<32000;t+=100)EXPECT_FALSE(h.observe(t,false));
  for(unsigned t=32000;t<62000;t+=100)EXPECT_FALSE(h.observe(t,true));
  EXPECT_TRUE(h.observe(62000,true));
}
TEST(BookStylePolicy, NoSyntheticIndentWithoutPublisherIndent) {
  auto firstX=[](bool automatic) {
    ParsedText text(false);
    text.setAutomaticParagraphIndent(automatic);
    text.addWord("Alpha",EpdFontFamily::REGULAR,false,false,ParsedText::DEFAULT_WORD_SIZE_PCT,0);
    GfxRenderer renderer; int x=-1;
    text.layoutAndExtractLines(renderer,0,480,[&](std::unique_ptr<TextBlock> b,bool,bool) {
      x=b->wordXpos(0); return ParsedText::LineProcessResult::Accepted;
    },true);
    return x;
  };
  EXPECT_EQ(firstX(false),0);
  EXPECT_GT(firstX(true),0);
}

