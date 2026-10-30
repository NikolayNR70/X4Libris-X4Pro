// Fingerprint invalidation:
// the cache key is path-derived, so a book replaced in place must invalidate
// the cache, while an mtime-only touch must not. Observability: a canary file
// planted inside the cache dir survives iff the cache was NOT wiped.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

#include "Epub.h"

namespace fs = std::filesystem;

namespace {

const char* kBookA = CORPUS_DIR "/test_headings.epub";
const char* kBookB = CORPUS_DIR "/test_font_sizes.epub";

struct FingerprintFixture : testing::Test {
  fs::path work;
  fs::path bookPath;
  std::string cacheDir;

  void SetUp() override {
    // Per-test dir: ctest -j runs these cases as parallel processes, so a
    // shared path would make them clobber each other's caches.
    work = fs::temp_directory_path() /
           (std::string("epub_fingerprint_") + testing::UnitTest::GetInstance()->current_test_info()->name());
    fs::remove_all(work);
    fs::create_directories(work);
    bookPath = work / "book.epub";
    cacheDir = (work / "cache").string();
    fs::create_directories(cacheDir);
  }
  void TearDown() override { fs::remove_all(work); }

  // Loads the book at bookPath, returns its parsed title ("" on failure).
  std::string loadTitle() {
    Epub epub(bookPath.string(), cacheDir);
    if (!epub.load(true)) return {};
    return epub.getTitle();
  }

  fs::path epubCacheRoot() {
    // Per-book caches now live beneath the shared `tmp` directory. Keep the
    // canary/fingerprint assertions attached to the EPUB cache, not that parent.
    const fs::path sharedTmp = fs::path(cacheDir) / "tmp";
    if (!fs::is_directory(sharedTmp)) return {};
    for (const auto& e : fs::directory_iterator(sharedTmp)) {
      if (e.is_directory() && e.path().filename().string().rfind("epub_", 0) == 0) return e.path();
    }
    return {};
  }

  void plantCanary() { std::ofstream(epubCacheRoot() / "canary") << "x"; }
  bool canaryAlive() { return fs::exists(epubCacheRoot() / "canary"); }
};

TEST_F(FingerprintFixture, ContentSwapAtSamePathRebuildsCache) {
  fs::copy_file(kBookA, bookPath);
  ASSERT_EQ(loadTitle(), "Heading Font Size Test");
  plantCanary();

  fs::remove(bookPath);
  fs::copy_file(kBookB, bookPath);
  // Same path, different bytes: the stale cache must be wiped and rebuilt —
  // the title MUST come from the new book, not the cached old one.
  EXPECT_EQ(loadTitle(), "Inline Font Size Test");
  EXPECT_FALSE(canaryAlive()) << "cache dir was not wiped on content change";
}

TEST_F(FingerprintFixture, MtimeTouchKeepsCache) {
  fs::copy_file(kBookA, bookPath);
  ASSERT_EQ(loadTitle(), "Heading Font Size Test");
  plantCanary();

  fs::last_write_time(bookPath, fs::file_time_type::clock::now());
  EXPECT_EQ(loadTitle(), "Heading Font Size Test");
  EXPECT_TRUE(canaryAlive()) << "mtime-only touch must not invalidate the cache";
}

TEST_F(FingerprintFixture, PreFingerprintCacheIsAdoptedNotWiped) {
  fs::copy_file(kBookA, bookPath);
  ASSERT_EQ(loadTitle(), "Heading Font Size Test");
  // Simulate a cache created by older firmware: no sidecar yet.
  fs::remove(epubCacheRoot() / "fingerprint.bin");
  plantCanary();

  EXPECT_EQ(loadTitle(), "Heading Font Size Test");
  EXPECT_TRUE(canaryAlive()) << "upgrade must adopt existing caches, not mass-invalidate";
  EXPECT_TRUE(fs::exists(epubCacheRoot() / "fingerprint.bin")) << "sidecar not written on adoption";
}

TEST_F(FingerprintFixture, NeedsFirstOpenIndexingReportsContentSwap) {
  fs::copy_file(kBookA, bookPath);
  ASSERT_EQ(loadTitle(), "Heading Font Size Test");
  {
    Epub epub(bookPath.string(), cacheDir);
    EXPECT_FALSE(epub.needsFirstOpenIndexing());
  }
  fs::remove(bookPath);
  fs::copy_file(kBookB, bookPath);
  {
    Epub epub(bookPath.string(), cacheDir);
    EXPECT_TRUE(epub.needsFirstOpenIndexing()) << "caller must get its progress popup before the rebuild";
  }
}

TEST_F(FingerprintFixture, MalformedZipIsRejectedBeforeBookCacheIsBuilt) {
  {
    std::ofstream out(bookPath, std::ios::binary);
    out << "not a zip archive";
  }

  Epub epub(bookPath.string(), cacheDir);
  EXPECT_FALSE(epub.load(true));
  EXPECT_FALSE(fs::exists(fs::path(epub.getCachePath()) / "book.bin"))
      << "an invalid source must not leave a readable book cache";
  EXPECT_FALSE(fs::exists(fs::path(epub.getCachePath()) / "spine.bin.tmp"))
      << "a failed OPF parse must not leave a temporary spine file";
  EXPECT_FALSE(fs::exists(fs::path(epub.getCachePath()) / "toc.bin.tmp"))
      << "a failed OPF parse must not leave a temporary TOC file";
}

TEST_F(FingerprintFixture, InvalidCentralDirectoryOffsetIsRejected) {
  fs::copy_file(kBookA, bookPath);
  std::ifstream input(bookPath, std::ios::binary);
  const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  ASSERT_FALSE(bytes.empty());

  const std::string eocdSignature("\x50\x4b\x05\x06", 4);
  const size_t eocd = bytes.rfind(eocdSignature);
  ASSERT_NE(eocd, std::string::npos);
  ASSERT_LE(eocd + 22, bytes.size());

  std::fstream archive(bookPath, std::ios::binary | std::ios::in | std::ios::out);
  ASSERT_TRUE(archive);
  archive.seekp(static_cast<std::streamoff>(eocd + 16));  // central directory offset in EOCD
  const char invalidOffset[4] = {static_cast<char>(0xff), static_cast<char>(0xff), static_cast<char>(0xff),
                                 static_cast<char>(0xff)};
  archive.write(invalidOffset, sizeof(invalidOffset));
  archive.close();
  ASSERT_TRUE(archive);

  Epub epub(bookPath.string(), cacheDir);
  EXPECT_FALSE(epub.load(true));
  EXPECT_FALSE(fs::exists(fs::path(epub.getCachePath()) / "book.bin"))
      << "an archive with an invalid central-directory offset must not leave a readable cache";
  EXPECT_FALSE(fs::exists(fs::path(epub.getCachePath()) / "spine.bin.tmp"));
  EXPECT_FALSE(fs::exists(fs::path(epub.getCachePath()) / "toc.bin.tmp"));
}

}  // namespace
