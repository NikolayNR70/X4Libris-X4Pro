// Blocks laid out inside a heading, and <span>s the book's CSS turns into blocks (issue #388).
//
// St. Martin's chapter openers put the number and the title in two spans styled display:block
// inside one <h1>:
//
//   <h1 class="CHAPTER"><span class="CN">10</span><span class="CT">The Tempest</span></h1>
//
// Every reader stacks them, centered. The parser used to decide block-ness by tag name alone,
// so both spans flowed into one line, "10 The Tempest". Laying them out as blocks exposed a
// second gap: only the FIRST block inside a heading inherited its centering and size (it merges
// into the heading's still-empty block); the next started over with the reader's paragraph
// alignment, Justified by default.
//
// What has to hold:
//   1. Every block inside a heading keeps the heading's centering and size, unless the book's
//      own alignment applies (the "Book's style" setting) or it sets its own size.
//   2. The heading's scope ends with it: the paragraph after it is aligned as a paragraph.
//   3. A <span> styled display:block is a block: its own line, its own margins.
//   4. Its font size is applied once, by its block, not again per word.
//   5. Text after it in the same element resumes on a new line, in that element's style.
//   6. A <span> styled display:inline (or inline-block) stays inline.
#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "EpdFontFamily.h"
#include "Epub.h"
#include "Epub/Page.h"
#include "Epub/Section.h"
#include "GfxRenderer.h"
#include "StoredZipWriter.h"

namespace fs = std::filesystem;

namespace {

struct Line {
  int y;
  CssTextAlign align;
  float mult;
  std::string text;  // the line's words joined by single spaces
  uint8_t firstWordStyle;
  uint8_t firstWordSizePct;
  int x = 0;
};

struct HeadingBlockLayoutFixture : testing::Test {
  fs::path work;

  void SetUp() override {
    work = fs::temp_directory_path() /
           (std::string("epub_headblock_") + testing::UnitTest::GetInstance()->current_test_info()->name());
    fs::remove_all(work);
    fs::create_directories(work);
  }
  void TearDown() override { fs::remove_all(work); }

  std::string makeBook(const std::string& css, const std::string& body) {
    test_zip::StoredZipWriter zip;
    zip.add("mimetype", "application/epub+zip");
    zip.add("META-INF/container.xml",
            "<?xml version=\"1.0\"?>\n<container version=\"1.0\" "
            "xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n<rootfiles><rootfile "
            "full-path=\"content.opf\" media-type=\"application/oebps-package+xml\"/></rootfiles>\n</container>\n");
    zip.add("content.opf",
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<package xmlns=\"http://www.idpf.org/2007/opf\" "
            "version=\"3.0\" unique-identifier=\"id\">\n<metadata "
            "xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:identifier id=\"id\">headblock</dc:identifier>"
            "<dc:title>Heading blocks</dc:title><dc:language>en</dc:language></metadata>\n<manifest>\n"
            "<item id=\"c\" href=\"chapter.xhtml\" media-type=\"application/xhtml+xml\"/>\n"
            "<item id=\"s\" href=\"style.css\" media-type=\"text/css\"/>\n"
            "</manifest>\n<spine><itemref idref=\"c\"/></spine>\n</package>\n");
    zip.add("style.css", "p, h1, h2 { margin: 0; text-indent: 0; }\n" + css);
    zip.add("chapter.xhtml",
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<html xmlns=\"http://www.w3.org/1999/xhtml\">"
            "<head><title>C</title><link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\"/></head><body>\n" +
                body + "\n</body></html>\n");
    const std::string path = (work / "headblock.epub").string();
    zip.write(path);
    return path;
  }

  // Every text line of the chapter, in page order. `paragraphAlignment` is the reader setting:
  // 0 = Justified (the default), 4 = the book's own alignment.
  std::vector<Line> layout(const std::string& css, const std::string& body, const uint8_t paragraphAlignment = 0, int indent = -1, int gap = 50, bool extra = false, bool preserve = false, bool hyphens = false, int width = 480, bool bionic = false, int headingAlign = 0, int lineOverride = -1, int gapOverride = -1, int headingSize = -1, int height = 800, int heading2Size = -1) {
    auto epub = std::make_shared<Epub>(makeBook(css, body), (work / "cache").string());
    EXPECT_TRUE(epub->load(true));
    Section::BuildParams params;
    params.viewportWidth = width;
    params.hyphenationEnabled = hyphens;
    params.bionicReadingEnabled = bionic;
    params.viewportHeight = height;
    params.lineCompression = 1.0f;
    params.embeddedStyle = true;
    params.preserveBookStyle = preserve;
    if (preserve) params.fontSizeNormalization = false;
    params.paragraphAlignment = paragraphAlignment;
    params.headingAlignment=headingAlign;
    params.lineHeightOverridePercent=lineOverride;
    params.paragraphGapOverridePercent=gapOverride;
    params.headingSizeOverridePercent=headingSize;
    params.heading2SizeOverridePercent=heading2Size;
    params.paragraphIndentPercent = indent;
    params.paragraphGapPercent = gap;
    params.extraParagraphSpacing = extra;
    GfxRenderer renderer;
    Section section(epub, 0, renderer);
    EXPECT_TRUE(section.createSectionFile(params, {}, /*skipEviction=*/true));
    EXPECT_TRUE(section.loadSectionFile(params));
    std::vector<Line> lines;
    for (uint16_t p = 0; p < section.pageCount; ++p) {
      section.currentPage = p;
      const auto page = section.loadPageFromSectionFile();
      if (!page) continue;
      for (const auto& el : page->elements) {
        if (el->getTag() != TAG_PageLine) continue;
        const TextBlock& block = *static_cast<const PageLine&>(*el).getBlock();
        if (block.wordCount() == 0) continue;
        Line line{el->yPos, block.getRenderStyle().alignment,         block.getRenderStyle().fontSizeMultiplier,
                  {},       static_cast<uint8_t>(block.wordStyle(0)), block.wordSizePct(0), block.wordXpos(0)};
        for (uint16_t w = 0; w < block.wordCount(); ++w) {
          if (w > 0) line.text += ' ';
          line.text += block.wordText(w);
        }
        lines.push_back(line);
      }
    }
    return lines;
  }
};

const char* alignName(const CssTextAlign a) {
  switch (a) {
    case CssTextAlign::Justify:
      return "justify";
    case CssTextAlign::Left:
      return "left";
    case CssTextAlign::Center:
      return "center";
    case CssTextAlign::Right:
      return "right";
    case CssTextAlign::None:
      return "none";
  }
  return "?";
}

}  // namespace

TEST_F(HeadingBlockLayoutFixture, EveryBlockInAHeadingIsCentered) {
  const auto lines = layout("", "<h1><div>Alpha</div><div>Bravo</div></h1><p>Body text</p>");
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0].text, "Alpha");
  EXPECT_STREQ(alignName(lines[0].align), "center");
  EXPECT_EQ(lines[1].text, "Bravo");
  EXPECT_STREQ(alignName(lines[1].align), "center") << "the second block in the heading lost its centering";
  EXPECT_EQ(lines[2].text, "Body text");
  EXPECT_STREQ(alignName(lines[2].align), "justify") << "the heading's centering leaked past </h1>";
}

// Under the Justified setting a book's text-align is overridden for paragraphs. A block in a
// heading is heading text, so it stays centered rather than taking the paragraph setting.
TEST_F(HeadingBlockLayoutFixture, BookAlignedBlockInAHeadingStaysCenteredUnderJustified) {
  const auto lines =
      layout("div.cn { text-align: center; }\n", "<h1><div class=\"cn\">Alpha</div><div class=\"cn\">Bravo</div></h1>");
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_STREQ(alignName(lines[0].align), "center");
  EXPECT_STREQ(alignName(lines[1].align), "center");
}

// With the "Book's style" setting the book's own text-align on the block wins, as it does for
// the heading itself.
TEST_F(HeadingBlockLayoutFixture, BooksStyleSettingKeepsTheBlocksOwnAlignment) {
  const auto lines = layout("div.left { text-align: left; }\n",
                            "<h1><div class=\"left\">Alpha</div><div class=\"left\">Bravo</div></h1>",
                            /*paragraphAlignment=*/4);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_STREQ(alignName(lines[0].align), "left");
  EXPECT_STREQ(alignName(lines[1].align), "left");
}

TEST_F(HeadingBlockLayoutFixture, EveryBlockInAHeadingKeepsTheHeadingSize) {
  const auto lines = layout("", "<h2><div>Alpha</div><div>Bravo</div></h2>");
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_GT(lines[0].mult, 1.0f) << "an h2 without a CSS size should get the default heading size";
  EXPECT_EQ(lines[1].mult, lines[0].mult) << "the second block in the heading lost the heading size";
}

TEST_F(HeadingBlockLayoutFixture, LevelTwoAlignmentAndSizeApplyInsideNestedParagraphs) {
  const auto lines = layout("", "<h2><p>Section two</p></h2>", 4, -1, 0, false, true, false, 480, false, 2,
                            -1, -1, -1, 800, 160);
  ASSERT_FALSE(lines.empty());
  EXPECT_EQ(lines.front().align, CssTextAlign::Left);
  EXPECT_FLOAT_EQ(lines.front().mult, 1.6f);
}

// A size the block sets for itself wins over the heading's.
TEST_F(HeadingBlockLayoutFixture, BlockInAHeadingKeepsItsOwnSize) {
  const auto lines = layout("div.big { font-size: 2em; }\n", "<h2><div>Alpha</div><div class=\"big\">Bravo</div></h2>");
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_GT(lines[1].mult, lines[0].mult);
}

namespace {
// The #388 book's chapter-head rules, trimmed to what layout reads.
constexpr const char* kChapterHeadCss =
    "h1.CHAPTER { font-size: 100%; display: inline; }\n"
    "span.CN { display: block; font-size: 1.2em; font-weight: bold; text-align: center; }\n"
    "span.CT { display: block; font-size: 1.4em; font-weight: bold; text-align: center; }\n"
    "span.gap { margin-bottom: 2em; }\n";
}  // namespace

TEST_F(HeadingBlockLayoutFixture, BlockSpansInAHeadingStackCentered) {
  const auto lines = layout(kChapterHeadCss,
                            "<h1 class=\"CHAPTER\">\n"
                            "<span class=\"CN\"><a href=\"#t\"><span>10</span></a></span>\n"
                            "<span class=\"CT\"><a href=\"#t\"><span>The Tempest</span></a></span>\n"
                            "</h1>\n<p id=\"t\">They struggled along</p>");
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0].text, "10");
  EXPECT_STREQ(alignName(lines[0].align), "center");
  EXPECT_EQ(lines[1].text, "The Tempest");
  EXPECT_STREQ(alignName(lines[1].align), "center");
  EXPECT_EQ(lines[2].text, "They struggled along");
  EXPECT_STREQ(alignName(lines[2].align), "justify");
  EXPECT_EQ(lines[2].mult, 1.0f) << "the heading's size leaked into the paragraph after it";
}

TEST_F(HeadingBlockLayoutFixture, BlockSpanKeepsItsMargin) {
  const std::string body = "<h1 class=\"CHAPTER\"><span class=\"CN %s\">10</span><span class=\"CT\">Title</span></h1>";
  auto withClass = [&](const char* extra) {
    std::string b = body;
    b.replace(b.find("%s"), 2, extra);
    return layout(kChapterHeadCss, b);
  };
  const auto plain = withClass("");
  const auto spaced = withClass("gap");
  ASSERT_EQ(plain.size(), 2u);
  ASSERT_EQ(spaced.size(), 2u);
  EXPECT_GT(spaced[1].y - spaced[0].y, plain[1].y - plain[0].y) << "the span's margin-bottom was dropped";
}

TEST_F(HeadingBlockLayoutFixture, BlockSpanSizeIsAppliedOnce) {
  const auto lines =
      layout("span.big { display: block; font-size: 2em; }\n", "<p>Body <span class=\"big\">Large</span></p>");
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[1].text, "Large");
  EXPECT_NEAR(lines[1].mult * lines[1].firstWordSizePct / 100.0f, 2.0f, 0.01f)
      << "mult=" << lines[1].mult << " word size=" << int(lines[1].firstWordSizePct) << "%";
}

TEST_F(HeadingBlockLayoutFixture, TextAfterABlockSpanResumesOnItsOwnLine) {
  const auto lines = layout("p.it { font-style: italic; } span.blk { display: block; font-weight: bold; }\n",
                            "<p class=\"it\">Before <span class=\"blk\">middle</span> after</p>");
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0].text, "Before");
  EXPECT_EQ(lines[1].text, "middle");
  EXPECT_EQ(lines[2].text, "after");
  EXPECT_EQ(lines[2].firstWordStyle, EpdFontFamily::ITALIC) << "the paragraph's italic did not resume after the span";
}

TEST_F(HeadingBlockLayoutFixture, InlineSpanStaysInline) {
  const auto lines = layout("span.a { display: inline; font-weight: bold; } span.b { display: inline-block; }\n",
                            "<p>One <span class=\"a\">two</span> <span class=\"b\">three</span> four</p>");
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0].text, "One two three four");
}

TEST_F(HeadingBlockLayoutFixture, CustomIndentOverridesPublisherAndKeepsHeading) {
  auto lines = layout("p {text-indent:0;}", "<h1>Title</h1><p>Alpha bravo</p><p>Charlie delta</p>", 0, 200);
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0].align, CssTextAlign::Center);
  EXPECT_GT(lines[1].x, 0);
  EXPECT_EQ(lines[1].x, lines[2].x);
}
TEST_F(HeadingBlockLayoutFixture, ZeroCustomIndentRemovesPublisherIndent) {
  auto lines = layout("p {text-indent:2em;}", "<p>Alpha bravo</p><p>Charlie delta</p>", 0, 0);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0].x, 0);
  EXPECT_EQ(lines[1].x, 0);
}
TEST_F(HeadingBlockLayoutFixture, ParagraphGapChangesRealLayout) {
  auto a = layout("", "<p>Alpha</p><p>Bravo</p>", 0, -1, 0, true);
  auto b = layout("", "<p>Alpha</p><p>Bravo</p>", 0, -1, 100, true);
  ASSERT_EQ(a.size(), 2u); ASSERT_EQ(b.size(), 2u);
  EXPECT_GT(b[1].y-a[1].y, 0);
}

TEST_F(HeadingBlockLayoutFixture, BookModeKeepsPublisherAlignmentIndentAndSize) {
  auto lines=layout("p {text-align:right;font-size:1.5em;} p.left {text-align:left;text-indent:2em;}",
    "<p>Alpha</p><p class=\"left\">Bravo</p>",4,-1,0,false,true);
  ASSERT_EQ(lines.size(),2u);
  EXPECT_EQ(lines[0].align,CssTextAlign::Right);
  EXPECT_EQ(lines[1].align,CssTextAlign::Left);
  EXPECT_GT(lines[1].x,0);
  EXPECT_NEAR(lines[0].mult*lines[0].firstWordSizePct/100.f,1.5f,.01f);
}
TEST_F(HeadingBlockLayoutFixture, BookModeKeepsPublisherParagraphSpacing) {
  auto a=layout("p {margin-bottom:0;}","<p>Alpha</p><p>Bravo</p>",4,-1,0,false,true);
  auto b=layout("p {margin-bottom:2em;}","<p>Alpha</p><p>Bravo</p>",4,-1,0,false,true);
  ASSERT_EQ(a.size(),2u); ASSERT_EQ(b.size(),2u);
  EXPECT_GT(b[1].y-a[1].y,0);
}

#include <cstdlib>
#include <iostream>
#include "Epub/css/CssCascade.h"
TEST_F(HeadingBlockLayoutFixture, CascadeBookTitleUsesDescendantRulesAndInheritance) {
  auto lines=layout("body {font-size:12pt;line-height:1.4;} p {text-indent:1.3em;} body p {text-align:justify !important;} .title p {font-size:1.35em;} body .title p {text-align:center !important;text-indent:0 !important;font-weight:bold !important;} body .heading-1, body .heading-1 p {font-size:16pt !important;}",
    "<section><header class=\"title heading-1\"><p>Author</p><p>Title</p></header><p>Prose here</p></section>",4,-1,0,false,true);
  ASSERT_EQ(lines.size(),3u);
  for(int i=0;i<2;++i) {EXPECT_EQ(lines[i].align,CssTextAlign::Center);EXPECT_EQ(lines[i].firstWordStyle,EpdFontFamily::BOLD);EXPECT_NEAR(lines[i].mult*lines[i].firstWordSizePct/100.f,4.f/3.f,.02f);}
  EXPECT_EQ(lines[2].align,CssTextAlign::Justify);EXPECT_EQ(lines[2].firstWordStyle,EpdFontFamily::REGULAR);
  EXPECT_NEAR(lines[2].mult*lines[2].firstWordSizePct/100.f,1.f,.01f);EXPECT_GT(lines[2].x,0);
}
TEST_F(HeadingBlockLayoutFixture, ReaderHeadingChoiceCentersBoldOnlyFb2SectionTitles) {
  auto lines = layout("p {text-align:left;} .fb2-heading-2 {text-align:left;}",
                      "<p class=\"fb2-heading-2 heading-2\"><strong>Section title</strong></p>"
                      "<p>Body remains left</p>",
                      1, -1, 50, false, false, false, 480, false, 1);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0].text, "Section title");
  EXPECT_EQ(lines[0].align, CssTextAlign::Center);
  EXPECT_EQ(lines[0].firstWordStyle, EpdFontFamily::BOLD);
  EXPECT_EQ(lines[1].text, "Body remains left");
  EXPECT_EQ(lines[1].align, CssTextAlign::Left);
}
TEST_F(HeadingBlockLayoutFixture, CascadeImportantSpecificitySourceOrderAndInline) {
  auto lines=layout("p {text-align:right !important;} .a p {text-align:center !important;} .a p {font-weight:bold;} .a p {font-style:italic;} p {text-align:left;}",
     "<div class=\"a\"><p style=\"text-align:right\">First</p><p style=\"text-align:left !important\">Second</p></div><p>Third</p>",4,-1,0,false,true);
  ASSERT_EQ(lines.size(),3u);EXPECT_EQ(lines[0].align,CssTextAlign::Center);EXPECT_EQ(lines[1].align,CssTextAlign::Left);EXPECT_EQ(lines[2].align,CssTextAlign::Right);
  EXPECT_EQ(lines[0].firstWordStyle,EpdFontFamily::BOLD_ITALIC);
}
TEST(CssCascadeSelectors, DescendantChildCompoundUniversalAndRejectedSyntax) {
  std::vector<CssCascade::Element> path={{"body","",""},{"section","a b","root"},{"div","nested",""},{"p","x y","paragraph"}};
  EXPECT_GE(CssCascade::specificity("body .a.b p.x#paragraph",path),0);
  EXPECT_GE(CssCascade::specificity("section > div > *",path),0);
  EXPECT_LT(CssCascade::specificity("section > p",path),0);
  EXPECT_LT(CssCascade::specificity(".b .a p",path),0);
  EXPECT_FALSE(CssCascade::usable("p:hover"));EXPECT_FALSE(CssCascade::usable("p + p"));
  EXPECT_FALSE(CssCascade::usable("body >"));
}
TEST_F(HeadingBlockLayoutFixture, CascadeLineHeightChangesRealLinePositions) {
  auto a=layout("p {line-height:1;}","<p>Alpha</p><p>Bravo</p>",4,-1,0,false,true);
  fs::remove_all(work/"cache");
  auto b=layout("p {line-height:2;}","<p>Alpha</p><p>Bravo</p>",4,-1,0,false,true);
  ASSERT_EQ(a.size(),2u);ASSERT_EQ(b.size(),2u);
  EXPECT_EQ(b[1].y-b[0].y,2*(a[1].y-a[0].y));
}
TEST_F(HeadingBlockLayoutFixture, CascadeInheritedLineHeightAndSizeDoNotLeakToSibling) {
  auto lines=layout("div.big {font-size:1.5em;line-height:2;font-style:italic;} .big p {font-size:inherit;} .reset {font-size:initial;font-style:normal;}",
      "<div class=\"big\"><p>Alpha</p><p>Bravo</p></div><p class=\"reset\">Charlie</p>",4,-1,0,false,true);
  ASSERT_EQ(lines.size(),3u);EXPECT_NEAR(lines[0].mult*lines[0].firstWordSizePct/100.f,1.5f,.02f);
  EXPECT_EQ(lines[0].firstWordStyle,EpdFontFamily::ITALIC);EXPECT_EQ(lines[2].firstWordStyle,EpdFontFamily::REGULAR);
  EXPECT_NEAR(lines[2].mult*lines[2].firstWordSizePct/100.f,1.f,.01f);EXPECT_GT(lines[1].y-lines[0].y,30);
}
TEST_F(HeadingBlockLayoutFixture, CascadeNestedRelativeInlineSizesApplyOnce) {
  auto lines=layout("p {font-size:1.5em;} span.outer {font-size:1.2em;} span.inner {font-size:1.1em;}",
      "<p><span class=\"outer\"><span class=\"inner\">Word</span></span></p>",4,-1,0,false,true);
  ASSERT_EQ(lines.size(),1u);EXPECT_NEAR(lines[0].mult*lines[0].firstWordSizePct/100.f,1.5f*1.2f*1.1f,.04f);
}
TEST_F(HeadingBlockLayoutFixture, CascadeActualProblemBook) {
  const char* path=std::getenv("PROBLEM_EPUB");if(!path) GTEST_SKIP()<<"Set PROBLEM_EPUB to the supplied EPUB";
  auto epub=std::make_shared<Epub>(path,(work/"actual-cache").string());ASSERT_TRUE(epub->load(true));
  Section::BuildParams p;p.embeddedStyle=true;p.preserveBookStyle=true;p.paragraphAlignment=4;
  p.fontSizeNormalization=false;p.extraParagraphSpacing=false;p.paragraphGapPercent=0;
  p.viewportWidth=480;p.viewportHeight=800;p.lineCompression=1.f;
  GfxRenderer renderer;size_t totalPages=0,totalWords=0;bool bodySeen=false;
  for(int spine=0;spine<epub->getSpineItemsCount();++spine) {
    Section section(epub,spine,renderer);ASSERT_TRUE(section.createSectionFile(p,{},true));ASSERT_TRUE(section.loadSectionFile(p));
    totalPages+=section.pageCount;
    for(int pg=0;pg<section.pageCount;++pg) {
      section.currentPage=pg;auto page=section.loadPageFromSectionFile();ASSERT_TRUE(page);
      for(const auto& el:page->elements) if(el->getTag()==TAG_PageLine) {
        const auto& b=*static_cast<const PageLine&>(*el).getBlock();totalWords+=b.wordCount();
        if(!b.wordCount())continue;
        const auto& s=b.getRenderStyle();
        if(spine==0 && !bodySeen) {
          if(b.wordStyle(0)==EpdFontFamily::BOLD) {EXPECT_EQ(s.alignment,CssTextAlign::Center);EXPECT_NEAR(s.fontSizeMultiplier*b.wordSizePct(0)/100.f,4.f/3.f,.02f);}
          else {bodySeen=true;EXPECT_EQ(s.alignment,CssTextAlign::Justify);EXPECT_NEAR(s.fontSizeMultiplier,1.f,.01f);}
        }
      }
    }
  }
  EXPECT_TRUE(bodySeen);EXPECT_GT(totalPages,10u);EXPECT_GT(totalWords,1000u);
  std::cout<<"ACTUAL_BOOK pages="<<totalPages<<" words="<<totalWords<<"\n";
}


TEST_F(HeadingBlockLayoutFixture, CascadeUnitlessLineHeightTracksChildSizeButPercentageStaysFixed) {
  const char* body="<div class=\"wrapper\"><p>Alpha</p><p>Bravo</p></div>";
  auto fixed=layout(".wrapper {font-size:1.5em;line-height:200%;} p {font-size:2em;}",body,4,-1,0,false,true);
  fs::remove_all(work/"cache");
  auto relative=layout(".wrapper {font-size:1.5em;line-height:2;} p {font-size:2em;}",body,4,-1,0,false,true);
  ASSERT_EQ(fixed.size(),2u);ASSERT_EQ(relative.size(),2u);
  EXPECT_EQ(relative[1].y-relative[0].y,2*(fixed[1].y-fixed[0].y));
}
TEST_F(HeadingBlockLayoutFixture, CascadeVisibleDeclarationCancelsEarlierTransparentValue) {
  auto lines=layout("p {color:transparent;} p.visible {color:black;}",
      "<p>Hidden</p><p class=\"visible\">Visible</p><p style=\"color:black\">Inline</p>",4,-1,0,false,true);
  ASSERT_EQ(lines.size(),2u);EXPECT_EQ(lines[0].text,"Visible");EXPECT_EQ(lines[1].text,"Inline");
}
TEST_F(HeadingBlockLayoutFixture, CascadeTruncatedStyleRecordsFailWithoutSavingUnstyledPages) {
  auto epub=std::make_shared<Epub>(makeBook(".title p {text-align:center;}","<div class=\"title\"><p>Alpha</p></div>"),(work/"cache").string());
  ASSERT_TRUE(epub->load(true));
  const auto cssFile=epub->getCachePath()+"/css_cascade.bin";
  ASSERT_GT(fs::file_size(cssFile),6u);fs::resize_file(cssFile,6);
  Section::BuildParams p;p.embeddedStyle=true;p.preserveBookStyle=true;p.paragraphAlignment=4;
  p.fontSizeNormalization=false;p.viewportWidth=480;p.viewportHeight=800;
  GfxRenderer renderer;Section section(epub,0,renderer);
  EXPECT_FALSE(section.createSectionFile(p,{},true));EXPECT_FALSE(section.loadSectionFile(p));
}


TEST_F(HeadingBlockLayoutFixture, HyphensNoneInheritedInlineAndImportant) {
  const std::string word="characteristically";
  auto autoLines=layout("", "<p>characteristically characteristically characteristically</p>",4,-1,0,false,true,true,160);
  bool split=false;for(const auto& l:autoLines) split=split || l.text.find('-')!=std::string::npos;
  EXPECT_TRUE(split);
  fs::remove_all(work/"cache");
  auto lines=layout(".off {hyphens:none !important;} .off p {hyphens:inherit;} .off span {hyphens:auto;}",
      "<div class=\"off\"><p>characteristically characteristically</p></div><p><span style=\"hyphens:none\">characteristically</span></p>",4,-1,0,false,true,true,160);
  for(const auto& l:lines) EXPECT_EQ(l.text.find('-'),std::string::npos)<<l.text;
}
TEST_F(HeadingBlockLayoutFixture, HyphensHeadingsProtectedWithoutCssAndBodyStillSplits) {
  auto lines=layout("", "<h1>characteristically characteristically</h1><div class=\"title\"><p>characteristically</p></div><p>characteristically characteristically</p>",4,-1,0,false,true,true,160);
  bool bodySplit=false;
  for(const auto& l:lines) {
    if(l.firstWordStyle & EpdFontFamily::BOLD || l.align==CssTextAlign::Center) EXPECT_EQ(l.text.find('-'),std::string::npos)<<l.text;
    else bodySplit=bodySplit || l.text.find('-')!=std::string::npos;
  }
  EXPECT_TRUE(bodySplit);
}
TEST_F(HeadingBlockLayoutFixture, HyphensNoneDoesNotLeakToFollowingSibling) {
  auto lines=layout(".off {-webkit-hyphens:none;}","<p class=\"off\">characteristically</p><p>characteristically characteristically</p>",4,-1,0,false,true,true,160);
  ASSERT_GE(lines.size(),2u);EXPECT_EQ(lines.front().text,"characteristically");
  bool split=false;for(size_t i=1;i<lines.size();++i) split=split || lines[i].text.find('-')!=std::string::npos;
  EXPECT_TRUE(split);
}

TEST_F(HeadingBlockLayoutFixture, HyphensPriorityAliasesAndManual) {
  for(const auto& css: {"p {hyphens:none !important;} p {hyphens:auto;}",
       "p {-epub-hyphens:none;}", "p {hyphens:manual;}", "p {hyphens:none;} p {hyphens:INVALID;}"}) {
    fs::remove_all(work/"cache");
    const auto lines=layout(css,"<p>characteristically characteristically</p>",4,-1,0,false,true,true,160);
    for(const auto& l:lines) EXPECT_EQ(l.text.find('-'),std::string::npos)<<css<<" "<<l.text;
  }
}
TEST_F(HeadingBlockLayoutFixture, HyphensAutoRestoresDescendantAndNoneSurvivesBionic) {
  auto lines=layout("div {hyphens:none;} p {hyphens:auto;}","<div><p>characteristically characteristically</p></div>",4,-1,0,false,true,true,160);
  bool split=false;for(const auto& l:lines) split=split || l.text.find('-')!=std::string::npos;EXPECT_TRUE(split);
  fs::remove_all(work/"cache");
  auto noSplit=layout("p {hyphens:none;}","<p>characteristically characteristically</p><p>characteristically</p>",4,-1,0,false,true,true,160,true);
  for(const auto& l:noSplit) EXPECT_EQ(l.text.find('-'),std::string::npos)<<l.text;
}
TEST_F(HeadingBlockLayoutFixture, HyphensManualSoftHyphenAndNoneSuppressesIt) {
  const std::string text="<p>character\xC2\xADistically character\xC2\xADistically</p>";
  auto manual=layout("p {hyphens:manual;}",text,4,-1,0,false,true,true,160);
  bool split=false;for(const auto& l:manual) split=split || l.text.find('-')!=std::string::npos;EXPECT_TRUE(split);
  fs::remove_all(work/"cache");
  auto none=layout("p {hyphens:none;}",text,4,-1,0,false,true,true,160);
  for(const auto& l:none) EXPECT_EQ(l.text.find('-'),std::string::npos)<<l.text;
}

TEST_F(HeadingBlockLayoutFixture, BodyAlignmentCannotOverridePublisherHeadingAlignment) {
  auto lines=layout("h1,.title {text-align:right;} .body {text-align:justify;}",
      "<h1>Heading one</h1><div class=\"title\"><p>Heading two</p></div><p>Body text</p>",1,-1,0,false,true);
  ASSERT_EQ(lines.size(),3u);
  EXPECT_EQ(lines[0].align,CssTextAlign::Right);EXPECT_EQ(lines[1].align,CssTextAlign::Right);
  EXPECT_EQ(lines[2].align,CssTextAlign::Left);
}
TEST_F(HeadingBlockLayoutFixture, ExplicitHeadingCenterAndSizeDoNotChangeBodyCss) {
  auto lines=layout("h1 {text-align:right;font-size:1.8em;} p {text-align:left;}",
      "<h1>Heading</h1><p>Body text</p>",4,-1,0,false,true,false,480,false,1,-1,-1,120);
  ASSERT_EQ(lines.size(),2u);
  EXPECT_EQ(lines[0].align,CssTextAlign::Center);EXPECT_NEAR(lines[0].mult,1.2f,.01f);
  EXPECT_EQ(lines[1].align,CssTextAlign::Left);EXPECT_NEAR(lines[1].mult,1.f,.01f);
}
TEST_F(HeadingBlockLayoutFixture, ExplicitLineHeightReplacesOnlyBodyLineHeight) {
  const std::string body="<p>Alpha<br/>Beta</p>";
  auto publisher=layout("p {line-height:2;}",body,4,-1,0,false,true);
  auto manual=layout("p {line-height:2;}",body,4,-1,0,false,true,false,480,false,0,100);
  ASSERT_EQ(publisher.size(),2u);ASSERT_EQ(manual.size(),2u);
  EXPECT_GT(publisher[1].y-publisher[0].y,manual[1].y-manual[0].y);
  EXPECT_GT(manual[1].y,manual[0].y);
}
TEST_F(HeadingBlockLayoutFixture, ExplicitZeroGapAndIndentRemainDistinctFromBookCss) {
  const std::string css="p {margin-top:2em;margin-bottom:2em;text-indent:2em;}";
  auto publisher=layout(css,"<p>Alpha</p><p>Beta</p>",4,-1,0,false,true);
  auto manual=layout(css,"<p>Alpha</p><p>Beta</p>",4,0,0,false,true,false,480,false,0,-1,0);
  ASSERT_EQ(publisher.size(),2u);ASSERT_EQ(manual.size(),2u);
  EXPECT_GT(publisher[0].x,manual[0].x);
  EXPECT_GT(publisher[1].y-publisher[0].y,manual[1].y-manual[0].y);
}
TEST_F(HeadingBlockLayoutFixture, EveryIndependentSelectionInvalidatesExistingLayout) {
  auto epub=std::make_shared<Epub>(makeBook("p {line-height:1.4;}","<h1>Head</h1><p>Text</p>"),(work/"cache").string());
  ASSERT_TRUE(epub->load(true));GfxRenderer renderer;Section section(epub,0,renderer);
  Section::BuildParams p;p.viewportWidth=480;p.viewportHeight=800;
  ASSERT_TRUE(section.createSectionFile(p,{},true));ASSERT_TRUE(section.loadSectionFile(p));
  auto changed=p;changed.headingAlignment=1;EXPECT_FALSE(section.loadSectionFile(changed));
  changed=p;changed.lineHeightOverridePercent=120;EXPECT_FALSE(section.loadSectionFile(changed));
  changed=p;changed.paragraphGapOverridePercent=0;EXPECT_FALSE(section.loadSectionFile(changed));
  changed=p;changed.headingSizeOverridePercent=140;EXPECT_FALSE(section.loadSectionFile(changed));
  changed=p;changed.heading2SizeOverridePercent=160;EXPECT_FALSE(section.loadSectionFile(changed));
  EXPECT_TRUE(section.loadSectionFile(p));
}

TEST_F(HeadingBlockLayoutFixture, PageBoundaryKeepsTheSameHyphenationAndWordPositions) {
  std::string body="<p>";
  for(int i=0;i<50;++i) body+="one inter&#173;national two con&#173;tinued ";
  body+="end</p>";
  const auto tall=layout("p { hyphens:auto; }",body,0,-1,0,false,false,true,190,false,0,-1,-1,-1,24000);
  const auto paged=layout("p { hyphens:auto; }",body,0,-1,0,false,false,true,190,false,0,-1,-1,-1,48);
  ASSERT_EQ(tall.size(),paged.size());
  bool boundaryHyphen=false;
  for(size_t i=0;i<tall.size();++i) {
    EXPECT_EQ(tall[i].text,paged[i].text)<<i;
    EXPECT_EQ(tall[i].x,paged[i].x)<<i;
    if(i%2==1 && !paged[i].text.empty() && paged[i].text.back()=='-') boundaryHyphen=true;
  }
  EXPECT_TRUE(boundaryHyphen);
}
