#include <Epub.h>
#include <Epub/Section.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "KOReaderSync/Fb2XPathMapper.h"
#include "KOReaderSync/ProgressMapper.h"
#include "StoredZipWriter.h"

TEST(DirectFb2Sync, UsesNativeFictionBookXPathAndResolvesItBackToSameTextOffset) {
  namespace fs = std::filesystem;
  const fs::path source = fs::temp_directory_path() / "fb2-sync-native-path.fb2";
  const fs::path cache = fs::temp_directory_path() / "fb2-sync-native-cache";
  fs::remove(source);
  fs::remove_all(cache);
  {
    std::ofstream out(source, std::ios::binary);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
           "<FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\">"
           "<description><title-info><book-title>Sync fixture</book-title><lang>en</lang></title-info></description>"
           "<body><section><title><p>First chapter</p></title>"
           "<p>Alpha text before the target. Beta text after the target.</p>"
           "<p>Another paragraph with enough text for a stable position.</p>"
           "</section><section><title><p>Second chapter</p></title>"
           "<p>Second chapter text remains in its own source section.</p></section></body></FictionBook>";
  }

  auto epub = std::make_shared<Epub>(source.string(), cache.string());
  ASSERT_TRUE(epub->load(true, false));
  ASSERT_TRUE(epub->isDirectFb2());
  ASSERT_GE(epub->getSpineItemsCount(), 2);
  ASSERT_FALSE(epub->getFb2ReaderBook().chapters[0].sourceXPath.empty());

  const int spine = 0;
  const uint32_t targetOffset = 12;
  CrossPointPosition local{};
  local.spineIndex = spine;
  local.pageNumber = 0;
  local.totalPages = 1;
  local.visibleTextOffset = targetOffset;
  local.hasVisibleTextOffset = true;
  const KOReaderPosition outgoing = ProgressMapper::toKOReader(epub, local);
  EXPECT_NE(outgoing.xpath.find("/FictionBook[1]/body[1]/section[1]"), std::string::npos) << outgoing.xpath;
  EXPECT_EQ(outgoing.xpath.find("DocFragment"), std::string::npos) << outgoing.xpath;

  int resolvedSpine = -1;
  uint32_t resolvedOffset = 0;
  bool exact = false;
  ASSERT_TRUE(Fb2XPathMapper::resolveXPath(*epub, outgoing.xpath, resolvedSpine, resolvedOffset, exact));
  EXPECT_EQ(resolvedSpine, spine);
  EXPECT_EQ(resolvedOffset, targetOffset);
  EXPECT_TRUE(exact);
  EXPECT_FALSE(Fb2XPathMapper::resolveXPath(*epub, "/FictionBook/body/section[9]/p/text()[1].0", resolvedSpine,
                                            resolvedOffset, exact));

  const KOReaderPosition nativeLike{outgoing.xpath, outgoing.percentage};
  const CrossPointPosition incoming = ProgressMapper::toCrossPoint(epub, nativeLike, 0, 1);
  EXPECT_EQ(incoming.spineIndex, spine);
  ASSERT_TRUE(incoming.hasVisibleTextOffset);
  EXPECT_EQ(incoming.visibleTextOffset, targetOffset);
  EXPECT_EQ(incoming.pageNumber, 0);  // The actual page is resolved from the offset after layout.

  CrossPointPosition secondChapter{};
  secondChapter.spineIndex = 1;
  secondChapter.pageNumber = 0;
  secondChapter.totalPages = 1;
  secondChapter.visibleTextOffset = 5;
  secondChapter.hasVisibleTextOffset = true;
  const KOReaderPosition secondOutgoing = ProgressMapper::toKOReader(epub, secondChapter);
  EXPECT_NE(secondOutgoing.xpath.find("/section[2]"), std::string::npos) << secondOutgoing.xpath;
  ASSERT_TRUE(Fb2XPathMapper::resolveXPath(*epub, secondOutgoing.xpath, resolvedSpine, resolvedOffset, exact));
  EXPECT_EQ(resolvedSpine, 1);
  EXPECT_EQ(resolvedOffset, 5u);

  const CrossPointPosition secondIncoming = ProgressMapper::toCrossPoint(epub, secondOutgoing, 0, 1);
  EXPECT_EQ(secondIncoming.spineIndex, 1);
  ASSERT_TRUE(secondIncoming.hasVisibleTextOffset);
  EXPECT_EQ(secondIncoming.visibleTextOffset, 5u);
  EXPECT_EQ(secondIncoming.pageNumber, 0);

  epub.reset();
  fs::remove(source);
  fs::remove_all(cache);
}

TEST(DirectFb2Sync, MapsOffsetsAcrossInlineElementsToNativeTextNodes) {
  namespace fs = std::filesystem;
  const fs::path source = fs::temp_directory_path() / "fb2-sync-inline-path.fb2";
  const fs::path cache = fs::temp_directory_path() / "fb2-sync-inline-cache";
  fs::remove(source);
  fs::remove_all(cache);
  {
    std::ofstream out(source, std::ios::binary);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
           "<FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\">"
           "<description><title-info><book-title>Sync fixture</book-title><lang>en</lang></title-info></description>"
           "<body><section><title><p>Chapter</p></title>"
           "<p>prefix <emphasis>inline target</emphasis> suffix</p>"
           "</section></body></FictionBook>";
  }

  auto epub = std::make_shared<Epub>(source.string(), cache.string());
  ASSERT_TRUE(epub->load(true, false));
  ASSERT_TRUE(epub->isDirectFb2());

  // Land inside "inline" after the heading and the paragraph break.
  constexpr uint32_t kOffsetInsideInlineText = 17;
  CrossPointPosition local{};
  local.spineIndex = 0;
  local.pageNumber = 0;
  local.totalPages = 1;
  local.visibleTextOffset = kOffsetInsideInlineText;
  local.hasVisibleTextOffset = true;

  const KOReaderPosition outgoing = ProgressMapper::toKOReader(epub, local);
  EXPECT_NE(outgoing.xpath.find("/emphasis[1]/text()[1]."), std::string::npos) << outgoing.xpath;

  int resolvedSpine = -1;
  uint32_t resolvedOffset = 0;
  bool exact = false;
  ASSERT_TRUE(Fb2XPathMapper::resolveXPath(*epub, outgoing.xpath, resolvedSpine, resolvedOffset, exact));
  EXPECT_EQ(resolvedSpine, 0);
  EXPECT_EQ(resolvedOffset, kOffsetInsideInlineText);
  EXPECT_TRUE(exact);

  const KOReaderPosition generatedXhtml{"/body/DocFragment[1]/body/div[1]/p[1]/text()[1].3", 0.5f};
  int generatedSpine = -1;
  uint32_t generatedOffset = 0;
  bool generatedExact = false;
  ASSERT_TRUE(
      Fb2XPathMapper::resolveXPath(*epub, generatedXhtml.xpath, generatedSpine, generatedOffset, generatedExact));
  const KOReaderPosition nativeText{"/FictionBook[1]/body[1]/section[1]/p[1]/text()[1].3", 0.5f};
  int nativeSpine = -1;
  uint32_t nativeOffset = 0;
  bool nativeExact = false;
  ASSERT_TRUE(Fb2XPathMapper::resolveXPath(*epub, nativeText.xpath, nativeSpine, nativeOffset, nativeExact));
  EXPECT_EQ(generatedSpine, 0);
  EXPECT_EQ(generatedOffset, nativeOffset);
  EXPECT_TRUE(generatedExact);
  EXPECT_TRUE(nativeExact);

  epub.reset();
  fs::remove(source);
  fs::remove_all(cache);
}

TEST(DirectFb2Sync, MapsKoReaderDocFragmentForFb2InsideZip) {
  namespace fs = std::filesystem;
  const fs::path source = fs::temp_directory_path() / "fb2-sync-archive.zip";
  const fs::path cache = fs::temp_directory_path() / "fb2-sync-archive-cache";
  fs::remove(source);
  fs::remove_all(cache);
  {
    test_zip::StoredZipWriter zip;
    zip.add(
        "books/chapter.fb2",
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\">"
        "<description><title-info><book-title>ZIP sync fixture</book-title><lang>en</lang></title-info></description>"
        "<body><section><title><p>Chapter</p></title><p>Text inside the archive is preserved.</p>"
        "</section></body></FictionBook>");
    zip.write(source.string());
  }

  auto epub = std::make_shared<Epub>(source.string(), cache.string());
  ASSERT_TRUE(epub->load(true, false));
  ASSERT_TRUE(epub->isDirectFb2());

  const KOReaderPosition incoming{"/body/DocFragment[1]/body/div[1]/p[1]/text()[1].8", 0.5f};
  const CrossPointPosition mapped = ProgressMapper::toCrossPoint(epub, incoming, 0, 1);
  ASSERT_EQ(mapped.spineIndex, 0);
  ASSERT_TRUE(mapped.hasVisibleTextOffset);
  EXPECT_GT(mapped.visibleTextOffset, 0u);

  const KOReaderPosition outgoing = ProgressMapper::toKOReader(epub, mapped);
  EXPECT_EQ(outgoing.xpath.find("/FictionBook[1]/body[1]/section[1]"), 0u) << outgoing.xpath;
  EXPECT_EQ(outgoing.xpath.find("DocFragment"), std::string::npos) << outgoing.xpath;

  int resolvedSpine = -1;
  uint32_t resolvedOffset = 0;
  bool exact = false;
  ASSERT_TRUE(Fb2XPathMapper::resolveXPath(*epub, outgoing.xpath, resolvedSpine, resolvedOffset, exact));
  EXPECT_EQ(resolvedSpine, 0);
  EXPECT_EQ(resolvedOffset, mapped.visibleTextOffset);
  EXPECT_TRUE(exact);

  epub.reset();
  fs::remove(source);
  fs::remove_all(cache);
}
