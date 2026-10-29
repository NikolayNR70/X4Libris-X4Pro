// KOReader sync round trips over the whole test corpus: every page of every chapter is uploaded
// (CrossPoint -> KOReader) and pulled back (KOReader -> CrossPoint), then snapped to a page the
// way the reader does it, and must come back on exactly the page it left.
//
// Two directions, two parsers, two ways of counting the chapter's text; the corpus has the
// shapes that break such pairs (wrapped paragraphs, headings, lists, tables, hidden text,
// image-only chapters, entities, ligatures). What has to hold:
//   1. Every page comes back on exactly itself, never ahead (which skips text) and never behind.
//      A page that starts in text pushes a text point (/text()[N].M: the page's first text to the
//      character, also inside inline elements); the pull resolves that text to its offset, and the
//      offset is the page's own start. The push never names an image: a page that starts with one
//      names the first text after it, which pulls back onto the page holding that text.
//
// The reader's snap (EpubReaderActivity::NavigationTarget::resolveInto) is mirrored here: a
// list-item index goes through the li LUT, then the paragraph LUT; a paragraph index through the
// paragraph LUT; a miss keeps the mapper's page estimate.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "Epub.h"
#include "Epub/Section.h"
#include "GfxRenderer.h"
#include "KOReaderSync/ChapterXPathIndexer.h"
#include "KOReaderSync/Fb2XPathMapper.h"
#include "KOReaderSync/ProgressMapper.h"

namespace fs = std::filesystem;

namespace {

Section::BuildParams params() {
  Section::BuildParams p;
  p.fontId = 0;
  p.lineCompression = 1.0f;
  p.extraParagraphSpacing = false;
  p.paragraphAlignment = 0;
  p.viewportWidth = 480;
  p.viewportHeight = 800;
  p.hyphenationEnabled = false;
  p.fontSizeNormalization = false;
  p.embeddedStyle = false;
  p.bionicReadingEnabled = false;
  p.inlineFootnotePreviews = false;
  p.imageRendering = 0;
  return p;
}

std::vector<std::string> corpusBooks() {
  std::vector<std::string> books;
  for (const auto& entry : fs::directory_iterator(CORPUS_DIR)) {
    if (entry.path().extension() == ".epub") books.push_back(entry.path().string());
  }
  std::sort(books.begin(), books.end());
  return books;
}

// The page the reader would open after applying `pos` to the chapter `section` is built for.
int snappedPage(const CrossPointPosition& pos, const Section& section) {
  if (pos.hasVisibleTextOffset) {
    if (const auto p = section.getPageForVisibleTextOffset(pos.visibleTextOffset)) return *p;
  }
  if (pos.hasListItemIndex) {
    if (const auto p = section.getPageForListItemIndex(pos.listItemIndex)) return *p;
    if (const auto p = section.getPageForParagraphIndex(pos.listItemIndex)) return *p;
    return pos.pageNumber;
  }
  if (pos.hasParagraphIndex) {
    if (const auto p = section.getPageForParagraphIndex(pos.paragraphIndex)) return *p;
  }
  return pos.pageNumber;
}

// What the reader pushes from `page`: the position plus the page's content offset when the LUT has one.
CrossPointPosition pushOf(const int spine, const int page, const int pages, const Section& section) {
  CrossPointPosition local{};
  local.spineIndex = spine;
  local.pageNumber = page;
  local.totalPages = pages;
  if (const auto off = section.getVisibleTextOffsetForPage(static_cast<uint16_t>(page))) {
    local.visibleTextOffset = *off;
    local.hasVisibleTextOffset = true;
  }
  return local;
}

struct RoundTripFixture : testing::TestWithParam<std::string> {
  fs::path work;
  std::shared_ptr<Epub> epub;
  GfxRenderer renderer;

  void SetUp() override {
    // One directory per test AND book: ctest runs the three tests of a book in parallel
    // processes, and they must not delete each other's cache.
    std::string tag = testing::UnitTest::GetInstance()->current_test_info()->name();
    for (char& c : tag) {
      if (c == '/') c = '_';
    }
    work = fs::temp_directory_path() / "progress_mapper_roundtrip" / tag;
    fs::remove_all(work);
    fs::create_directories(work);
    epub = std::make_shared<Epub>(GetParam(), (work / "cache").string());
    ASSERT_TRUE(epub->load(true)) << GetParam();
    epub->setupCacheDir();
  }
  void TearDown() override {
    epub.reset();
    fs::remove_all(work);
  }

  std::unique_ptr<Section> build(const int spine) {
    auto section = std::make_unique<Section>(epub, spine, renderer);
    EXPECT_TRUE(section->createSectionFile(params(), {}, /*skipEviction=*/true)) << "spine " << spine;
    EXPECT_TRUE(section->loadSectionFile(params())) << "spine " << spine;
    return section;
  }

  float spineStartPercentage(const int spine) const {
    const float start = spine > 0 ? static_cast<float>(epub->getCumulativeSpineItemSize(spine - 1)) : 0.0f;
    return start / static_cast<float>(epub->getBookSize());
  }
};

TEST_P(RoundTripFixture, EveryPageComesBackOnItself) {
  const int spineCount = epub->getSpineItemsCount();
  ASSERT_GT(spineCount, 0);
  for (int spine = 0; spine < spineCount; ++spine) {
    const auto section = build(spine);
    const int pages = section->pageCount;
    ASSERT_GT(pages, 0) << "spine " << spine;
    for (int page = 0; page < pages; ++page) {
      const auto ko = ProgressMapper::toKOReader(epub, pushOf(spine, page, pages, *section));
      const auto back = ProgressMapper::toCrossPoint(epub, ko, spine, pages);
      EXPECT_EQ(back.spineIndex, spine) << "page " << page << " via " << ko.xpath;
      if (back.spineIndex != spine) continue;
      const int landed = snappedPage(back, *section);
      EXPECT_EQ(landed, page) << "spine " << spine << " page " << page << "/" << pages << " came back as " << landed
                              << " (estimate " << back.pageNumber << ") via " << ko.xpath;
    }
  }
}

TEST_P(RoundTripFixture, LastPageComesBackAsTheLastPage) {
  // The failure that matters here is not a page of drift but a fall to page 0: the two passes
  // of the forward mapper counting the chapter's text differently would make the end of the
  // chapter unreachable, and the upload would name the chapter's root instead.
  const int spineCount = epub->getSpineItemsCount();
  for (int spine = 0; spine < spineCount; ++spine) {
    const auto section = build(spine);
    const int pages = section->pageCount;
    if (pages < 2) continue;
    const auto ko = ProgressMapper::toKOReader(epub, pushOf(spine, pages - 1, pages, *section));
    const auto back = ProgressMapper::toCrossPoint(epub, ko, spine, pages);
    EXPECT_EQ(back.spineIndex, spine);
    EXPECT_EQ(snappedPage(back, *section), pages - 1) << "spine " << spine << " via " << ko.xpath;
  }
}

TEST_P(RoundTripFixture, ChapterStartStaysOnFirstPageWhateverThePercentageSays) {
  // What a push from the start of a chapter on any device looks like, with KOReader's percentage
  // skewed either way by up to 5% of the book (issue #268 was +1.5%). The skew may even point
  // into the neighbouring chapter: the XPath still wins.
  constexpr float kSkews[] = {-0.05f, -0.011f, -0.01f, -0.001f, 0.0f, 0.001f, 0.01f, 0.011f, 0.05f};
  const int spineCount = epub->getSpineItemsCount();
  for (int spine = 0; spine < spineCount; ++spine) {
    const auto section = build(spine);
    const int pages = section->pageCount;
    // Pushed WITHOUT the page's offset on purpose: this test guards #268 (the percentage must not
    // override an exact XPath match), and an offset on the pull would land the page by itself.
    const auto ko = ProgressMapper::toKOReader(epub, {spine, 0, pages});
    for (const float skew : kSkews) {
      const float percentage = std::clamp(spineStartPercentage(spine) + skew, 0.0f, 1.0f);
      const auto back = ProgressMapper::toCrossPoint(epub, {ko.xpath, percentage}, spine, pages);
      EXPECT_EQ(back.spineIndex, spine) << "skew " << skew << " via " << ko.xpath;
      if (back.spineIndex != spine) continue;
      EXPECT_EQ(snappedPage(back, *section), 0) << "spine " << spine << " skew " << skew << " via " << ko.xpath;
    }
  }
}

// A page start that pushes as a text point comes back on exactly that offset; one that pushes as
// an element path resolves on or before it. This is the counting agreement between the parser's
// LUT and the two mappers, with no allowance. An element match is anchored at its first direct
// text or its END tag, which is no page start, so toCrossPoint does not hand its offset to the
// reader (the paragraph LUT or the estimate lands it); its anchor is checked at the resolver.
TEST_P(RoundTripFixture, EveryTextPointComesBackExactly) {
  const int spineCount = epub->getSpineItemsCount();
  int textPoints = 0;
  for (int spine = 0; spine < spineCount; ++spine) {
    const auto section = build(spine);
    const int pages = section->pageCount;
    for (int page = 0; page < pages; ++page) {
      const auto start = section->getVisibleTextOffsetForPage(static_cast<uint16_t>(page));
      if (!start) continue;
      CrossPointPosition pos{};
      pos.spineIndex = spine;
      pos.pageNumber = page;
      pos.totalPages = pages;
      pos.visibleTextOffset = *start;
      pos.hasVisibleTextOffset = true;
      const auto ko = ProgressMapper::toKOReader(epub, pos);
      if (ko.xpath.find("/text()[") == std::string::npos) {
        float intra = 0.0f;
        bool exact = false;
        uint32_t anchor = 0;
        ASSERT_TRUE(ChapterXPathIndexer::findProgressForXPath(epub, spine, ko.xpath, intra, exact, nullptr, &anchor))
            << "spine " << spine << " page " << page << " via " << ko.xpath;
        EXPECT_LE(anchor, *start) << "spine " << spine << " page " << page << " via " << ko.xpath;
        EXPECT_FALSE(ProgressMapper::toCrossPoint(epub, ko).hasVisibleTextOffset)
            << "spine " << spine << " page " << page << " via " << ko.xpath;
        continue;
      }
      ++textPoints;
      const auto back = ProgressMapper::toCrossPoint(epub, ko);
      ASSERT_TRUE(back.hasVisibleTextOffset) << "spine " << spine << " page " << page << " via " << ko.xpath;
      EXPECT_EQ(back.visibleTextOffset, *start) << "spine " << spine << " page " << page << " via " << ko.xpath;
    }
  }
  EXPECT_GT(textPoints, 0);
}

TEST(ProgressMapperRealFb2, LoggedPositionDoesNotResolveToBookStart) {
  const char* source = std::getenv("FB2_SYNC_FIXTURE");
  if (!source || !*source) GTEST_SKIP() << "set FB2_SYNC_FIXTURE to the real FB2 used for sync";

  const fs::path work = fs::temp_directory_path() / "progress_mapper_real_fb2_sync";
  fs::remove_all(work);
  fs::create_directories(work);
  auto epub = std::make_shared<Epub>(source, (work / "cache").string());
  ASSERT_TRUE(epub->load(true));
  epub->setupCacheDir();

  // Captured from the user's upload log: page 15/92, 1.39%, paragraph 58 in DocFragment 2.
  const KOReaderPosition received{"/body/DocFragment[2]/body/div[1]/p[58]/text()[1].235", 0.0139f};
  const auto mapped = ProgressMapper::toCrossPoint(epub, received, 1, 92);
  ASSERT_EQ(mapped.spineIndex, 1);
  ASSERT_TRUE(mapped.hasVisibleTextOffset) << "incoming DocFragment locator was not mapped to source FB2 text";
  {
    GfxRenderer renderer;
    Section section(epub, mapped.spineIndex, renderer);
    const auto layout = params();
    ASSERT_TRUE(section.createSectionFile(layout, {}, true));
    ASSERT_TRUE(section.loadSectionFile(layout));
    const auto page = section.getPageForVisibleTextOffset(mapped.visibleTextOffset);
    ASSERT_TRUE(page) << "received text offset " << mapped.visibleTextOffset << " was absent from target FB2 section";
    const int landed = *page;
    ASSERT_LT(landed, section.pageCount);
    std::cout << "Real FB2 remote position mapped to spine=" << mapped.spineIndex
              << " textOffset=" << (mapped.hasVisibleTextOffset ? mapped.visibleTextOffset : 0) << " page=" << landed
              << "/" << section.pageCount << "\n";
    EXPECT_GT(landed, 0) << "the real server position regressed to page 0";

    CrossPointPosition local{};
    local.spineIndex = mapped.spineIndex;
    local.pageNumber = landed;
    local.totalPages = section.pageCount;
    local.visibleTextOffset = section.getVisibleTextOffsetForPage(landed).value_or(mapped.visibleTextOffset);
    local.hasVisibleTextOffset = true;
    const auto outgoing = ProgressMapper::toKOReader(epub, local);
    int roundTripSpine = -1;
    uint32_t roundTripOffset = 0;
    bool exact = false;
    ASSERT_TRUE(Fb2XPathMapper::resolveXPath(*epub, outgoing.xpath, roundTripSpine, roundTripOffset, exact))
        << "outgoing FB2 XPath did not resolve: " << outgoing.xpath;
    EXPECT_EQ(roundTripSpine, local.spineIndex);
    EXPECT_TRUE(exact);
    EXPECT_EQ(roundTripOffset, local.visibleTextOffset) << "outgoing sync XPath did not preserve the page-start offset";
    std::cout << "Real FB2 outgoing sync payload: " << outgoing.percentage * 100.0f << "% at " << outgoing.xpath
              << " (offset=" << roundTripOffset << ")\n";
  }
  epub.reset();
  fs::remove_all(work);
}

INSTANTIATE_TEST_SUITE_P(Corpus, RoundTripFixture, testing::ValuesIn(corpusBooks()),
                         [](const testing::TestParamInfo<std::string>& info) {
                           std::string name = fs::path(info.param).stem().string();
                           for (char& c : name) {
                             if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
                           }
                           return name;
                         });

}  // namespace
