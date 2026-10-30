// Characterization tests for BookMetadataCache's TOC href -> spineIndex resolution.
//
// These pin the CURRENT behaviour so the allocation-hardening work (nothrow growth
// for the per-spine index containers) can be shown not to change any observable
// result. The property that matters: the fast binary-search index and the
// linear-scan fallback must resolve IDENTICALLY. The index is a pure speed
// optimisation, never correctness — so degrading to the scan on OOM must be safe.
//
// LARGE_SPINE_THRESHOLD is 16: a corpus below it exercises the linear scan, one
// above it exercises the index. Both are asserted against the same expectations.
//
// Resolution is observed by reading back the temp TOC file written by the first pass.
// Separate integration cases also exercise buildBookBin() against a real stored ZIP.

#include <HalStorage.h>
#include <Serialization.h>
#include <esp_heap_caps.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "AllocationRefusal.h"
#include "BatchLookupMemoryPolicy.h"
#include "BookMetadataCache.h"
#include "ChunkedNothrowArray.h"
#include "StoredZipWriter.h"

namespace fs = std::filesystem;

namespace {

class BookMetadataCacheTest : public testing::Test {
 protected:
  void SetUp() override {
    const testing::TestInfo* info = testing::UnitTest::GetInstance()->current_test_info();
    cacheDir_ = (fs::temp_directory_path() / ("bmc_test_" + std::string(info->name()))).string();
    fs::remove_all(cacheDir_);
    fs::create_directories(cacheDir_);
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(cacheDir_, ec);
  }

  std::string cacheDir_;
};

TEST(BookMetadataCacheMemoryPolicy, RequiresContiguousRoomForDequeMapEvenWhenTotalHeapIsLarge) {
  constexpr size_t spineCount = 1732;
  constexpr size_t pointerBytes = 4;  // ESP32 targets
  constexpr size_t targetBytes = 16;
  constexpr size_t sizeBytes = sizeof(uint32_t);
  const size_t tableAndReserve = spineCount * (targetBytes + sizeBytes) + epub_memory_policy::kBatchLookupReserveBytes;
  const size_t largestRequired = epub_memory_policy::dequeLargestBlockRequired(targetBytes, spineCount, pointerBytes);

  EXPECT_TRUE(epub_memory_policy::canUseBatchSpineSizeLookup(spineCount, tableAndReserve + 1, largestRequired,
                                                             targetBytes, sizeBytes, pointerBytes));
  EXPECT_FALSE(epub_memory_policy::canUseBatchSpineSizeLookup(spineCount, tableAndReserve + 1, largestRequired - 1,
                                                              targetBytes, sizeBytes, pointerBytes));
}

TEST(BookMetadataCacheMemoryPolicy, RefusesWhenTotalHeapCannotCoverTableAndReserve) {
  constexpr size_t spineCount = 1732;
  constexpr size_t pointerBytes = 4;
  constexpr size_t targetBytes = 16;
  constexpr size_t sizeBytes = sizeof(uint32_t);
  const size_t tableAndReserve = spineCount * (targetBytes + sizeBytes) + epub_memory_policy::kBatchLookupReserveBytes;
  const size_t largestRequired = epub_memory_policy::dequeLargestBlockRequired(targetBytes, spineCount, pointerBytes);

  EXPECT_FALSE(epub_memory_policy::canUseBatchSpineSizeLookup(spineCount, tableAndReserve, largestRequired, targetBytes,
                                                              sizeBytes, pointerBytes));
}

TEST(BookMetadataCacheMemoryPolicy, AppliesContiguousGuardToDifferentDequeElementTypes) {
  constexpr size_t spineCount = 1732;
  constexpr size_t firstElementBytes = 32;
  constexpr size_t secondElementBytes = 16;
  constexpr size_t pointerBytes = 4;
  const size_t required =
      std::max(epub_memory_policy::dequeLargestBlockRequired(firstElementBytes, spineCount, pointerBytes),
               epub_memory_policy::dequeLargestBlockRequired(secondElementBytes, spineCount, pointerBytes));
  const size_t free =
      spineCount * (firstElementBytes + secondElementBytes) + epub_memory_policy::kBatchLookupReserveBytes + 1;

  EXPECT_TRUE(epub_memory_policy::canUseBatchDequeLookup(spineCount, free, required, firstElementBytes,
                                                         secondElementBytes, pointerBytes));
  EXPECT_FALSE(epub_memory_policy::canUseBatchDequeLookup(spineCount, free, required - 1, firstElementBytes,
                                                          secondElementBytes, pointerBytes));
}

TEST(BookMetadataCacheMemoryPolicy, HandlesLargeCountsAndRejectsOverflow) {
  constexpr size_t pointerBytes = 4;
  const size_t normal = epub_memory_policy::dequeLargestBlockRequired(16, 1732, pointerBytes);
  const size_t larger = epub_memory_policy::dequeLargestBlockRequired(16, 5000, pointerBytes);
  EXPECT_GT(larger, normal);
  EXPECT_FALSE(epub_memory_policy::canUseBatchSpineSizeLookup(std::numeric_limits<size_t>::max(),
                                                              std::numeric_limits<size_t>::max(),
                                                              std::numeric_limits<size_t>::max(), 16, 4, pointerBytes));
  EXPECT_FALSE(epub_memory_policy::canUseBatchSpineSizeLookup(1, 1024 * 1024, 1024 * 1024, 0, 0, pointerBytes));
  EXPECT_EQ(epub_memory_policy::dequeLargestBlockRequired(0, 1732, pointerBytes), std::numeric_limits<size_t>::max());
}

TEST(BookMetadataCacheMemoryPolicy, AccountsForLibstdcxxMinimumMapAndLargeNodes) {
  constexpr size_t pointerBytes = 4;
  EXPECT_EQ(epub_memory_policy::dequeLargestBlockRequired(4, 0, pointerBytes), 8 * pointerBytes + 512);
  EXPECT_EQ(epub_memory_policy::dequeLargestBlockRequired(1024, 1, pointerBytes), 8 * pointerBytes + 1024);
}

// Read the spineIndex each TOC entry resolved to, in TOC order, straight out of
// the temp file the TOC pass wrote.
std::vector<int16_t> readResolvedSpineIndices(const std::string& cacheDir, size_t expectedCount) {
  std::vector<int16_t> out;
  FsFile toc;
  const std::string tocPath = cacheDir + "/toc.bin.tmp";
  if (!Storage.openFileForRead("TEST", tocPath, toc)) {
    ADD_FAILURE() << "could not open " << tocPath;
    return out;
  }
  toc.seek(0);
  for (size_t i = 0; i < expectedCount; i++) {
    std::string title, href, anchor;
    uint8_t level = 0;
    int16_t spineIndex = -1;
    serialization::readString(toc, title);
    serialization::readString(toc, href);
    serialization::readString(toc, anchor);
    serialization::readPod(toc, level);
    serialization::readPod(toc, spineIndex);
    out.push_back(spineIndex);
  }
  toc.close();
  return out;
}

// Drive the build-mode API the way Epub::load does: spine pass, then TOC pass. With
// `refuseAllocationsFrom`, the TOC entries are created on a heap that cannot serve a block that
// large. Armed after beginTocPass(): opening the temp files goes through the host storage shim,
// whose path conversion allocates more than the device's FsFile ever would.
std::vector<int16_t> resolveTocToSpine(const std::string& cacheDir, const std::vector<std::string>& spineHrefs,
                                       const std::vector<std::string>& tocHrefs, const size_t refuseAllocationsFrom = 0,
                                       size_t* tocEntryBytesRead = nullptr) {
  {
    BookMetadataCache cache(cacheDir);
    EXPECT_TRUE(cache.beginWrite());
    EXPECT_TRUE(cache.beginContentOpfPass());
    for (const std::string& href : spineHrefs) {
      cache.createSpineEntry(href);
    }
    EXPECT_TRUE(cache.endContentOpfPass());

    EXPECT_TRUE(cache.beginTocPass());
    std::optional<AllocationRefusal> refusal;
    if (refuseAllocationsFrom != 0) refusal.emplace(refuseAllocationsFrom);
    const size_t bytesReadBefore = FsFile::bytesRead;
    for (size_t i = 0; i < tocHrefs.size(); i++) {
      cache.createTocEntry("Title " + std::to_string(i), tocHrefs[i], /*anchor=*/"", /*level=*/0);
    }
    if (tocEntryBytesRead) *tocEntryBytesRead = FsFile::bytesRead - bytesReadBefore;
    EXPECT_TRUE(cache.endTocPass());
    refusal.reset();
    EXPECT_TRUE(cache.endWrite());
  }
  return readResolvedSpineIndices(cacheDir, tocHrefs.size());
}

std::vector<std::string> chapterHrefs(int count) {
  std::vector<std::string> hrefs;
  hrefs.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count; i++) {
    hrefs.push_back("OEBPS/chapter" + std::to_string(i) + ".xhtml");
  }
  return hrefs;
}

bool prepareSpineAndToc(BookMetadataCache& cache, const std::vector<std::string>& hrefs) {
  if (!cache.beginWrite() || !cache.beginContentOpfPass()) return false;
  for (const std::string& href : hrefs) cache.createSpineEntry(href);
  if (!cache.endContentOpfPass() || !cache.beginTocPass()) return false;
  for (size_t i = 0; i < hrefs.size(); ++i) {
    cache.createTocEntry("Chapter " + std::to_string(i), hrefs[i], /*anchor=*/"", /*level=*/0);
  }
  return cache.endTocPass() && cache.endWrite();
}

std::string writeSpineArchive(const std::string& cacheDir, const std::vector<std::string>& hrefs) {
  test_zip::StoredZipWriter zip;
  for (const std::string& href : hrefs) zip.add(href, "<html><body>text</body></html>");
  const std::string path = (fs::path(cacheDir) / "book.epub").string();
  zip.write(path);
  return path;
}

// --- Below LARGE_SPINE_THRESHOLD: the linear scan runs -----------------------

TEST_F(BookMetadataCacheTest, SmallCorpusResolvesEveryTocHrefViaLinearScan) {
  const std::vector<std::string> spine = chapterHrefs(8);
  const std::vector<int16_t> got = resolveTocToSpine(cacheDir_, spine, spine);

  ASSERT_EQ(got.size(), spine.size());
  for (size_t i = 0; i < got.size(); i++) {
    EXPECT_EQ(got[i], static_cast<int16_t>(i)) << "toc entry " << i;
  }
}

// --- Above the threshold: the fast binary-search index runs ------------------

TEST_F(BookMetadataCacheTest, LargeCorpusResolvesEveryTocHrefViaFastIndex) {
  const std::vector<std::string> spine = chapterHrefs(64);
  const std::vector<int16_t> got = resolveTocToSpine(cacheDir_, spine, spine);

  ASSERT_EQ(got.size(), spine.size());
  for (size_t i = 0; i < got.size(); i++) {
    EXPECT_EQ(got[i], static_cast<int16_t>(i)) << "toc entry " << i;
  }
}

// The core invariant the hardening must preserve: both modes agree.
TEST_F(BookMetadataCacheTest, FastIndexAndLinearScanAgreeOnTheSameTocHrefs) {
  const std::vector<std::string> toc = chapterHrefs(8);

  const std::vector<int16_t> viaScan = resolveTocToSpine(cacheDir_, chapterHrefs(8), toc);
  // Same TOC hrefs, but a spine large enough to trigger the index. The extra
  // spines are never referenced by the TOC and must not shift any resolution.
  const std::vector<int16_t> viaIndex = resolveTocToSpine(cacheDir_, chapterHrefs(64), toc);

  ASSERT_EQ(viaScan.size(), viaIndex.size());
  EXPECT_EQ(viaScan, viaIndex) << "the fast index resolved differently than the linear scan";
}

TEST_F(BookMetadataCacheTest, UnmatchedTocHrefResolvesToMinusOneInBothModes) {
  const std::vector<std::string> toc = {"OEBPS/chapter0.xhtml", "OEBPS/nowhere.xhtml"};

  const std::vector<int16_t> viaScan = resolveTocToSpine(cacheDir_, chapterHrefs(8), toc);
  ASSERT_EQ(viaScan.size(), 2u);
  EXPECT_EQ(viaScan[0], 0);
  EXPECT_EQ(viaScan[1], -1) << "an href with no spine match must resolve to -1, not to a neighbour";

  const std::vector<int16_t> viaIndex = resolveTocToSpine(cacheDir_, chapterHrefs(64), toc);
  ASSERT_EQ(viaIndex.size(), 2u);
  EXPECT_EQ(viaIndex[0], 0);
  EXPECT_EQ(viaIndex[1], -1);
}

// Many spines, sparse TOC — the shape the allocation hardening targets, since
// the index containers are sized by spineCount regardless of how small the TOC is.
TEST_F(BookMetadataCacheTest, LargeSpineCountWithSparseTocResolvesCorrectly) {
  const std::vector<std::string> spine = chapterHrefs(1000);
  const std::vector<std::string> toc = {spine[0], spine[499], spine[999]};

  const std::vector<int16_t> got = resolveTocToSpine(cacheDir_, spine, toc);
  ASSERT_EQ(got.size(), 3u);
  EXPECT_EQ(got[0], 0);
  EXPECT_EQ(got[1], 499);
  EXPECT_EQ(got[2], 999);
}

// --- A heap that cannot hold the index ----------------------------------------
//
// First-open indexing runs with the secondary framebuffer lent, not freed: ~50 KB of fragmented
// heap. A 2956-spine book's index (16 B/spine) does not fit there, and the deque that held it
// allocated through the throwing operator new -- abort() on the device. Refusing every block from
// 512 B up leaves the index (one block, 1 KB even for 64 spines) no room, while the small
// allocations a lookup makes still succeed. The 4 KB read/write buffers are refused as well and
// degrade to pass-through, as they would on such a heap.
constexpr size_t kIndexDoesNotFit = 512;

TEST_F(BookMetadataCacheTest, TocPassSurvivesAnIndexTheHeapCannotHold) {
  const std::vector<std::string> spine = chapterHrefs(64);
  const std::vector<int16_t> got = resolveTocToSpine(cacheDir_, spine, spine, kIndexDoesNotFit);

  ASSERT_EQ(got.size(), spine.size());
  for (size_t i = 0; i < got.size(); i++) {
    EXPECT_EQ(got[i], static_cast<int16_t>(i)) << "toc entry " << i;
  }
}

// Without the index, each TOC entry used to rescan the spine from its first record: ~4.4 million
// spine records for a 2956-entry TOC. A TOC lists its chapters in reading order, so resuming after
// the previous match finds the next one at once, and the whole TOC reads the spine file about
// once. Measured in bytes read, not time: on a host a from-the-start scan still finishes in under
// a second, on the device it is SD reads at 160 MHz.
TEST_F(BookMetadataCacheTest, AnInOrderTocReadsTheSpineAboutOnceWithoutTheIndex) {
  const std::vector<std::string> spine = chapterHrefs(3000);

  size_t bytesRead = 0;
  const std::vector<int16_t> got = resolveTocToSpine(cacheDir_, spine, spine, kIndexDoesNotFit, &bytesRead);

  ASSERT_EQ(got.size(), spine.size());
  for (size_t i = 0; i < got.size(); i++) {
    ASSERT_EQ(got[i], static_cast<int16_t>(i)) << "toc entry " << i;
  }
  const auto spineFileBytes = static_cast<size_t>(fs::file_size(fs::path(cacheDir_) / "spine.bin.tmp"));
  EXPECT_LE(bytesRead, 2 * spineFileBytes) << "spine file is " << spineFileBytes << " bytes";
}

// Resuming must not cost correctness: entries out of order, repeated, before the previous match
// (the scan wraps) or absent from the spine all resolve as a from-the-start scan would.
TEST_F(BookMetadataCacheTest, TheResumedScanResolvesAnyOrderLikeAFullScan) {
  const std::vector<std::string> spine = chapterHrefs(64);
  const std::vector<std::string> toc = {
      spine[5], spine[2], spine[63], spine[63], spine[0], spine[40], "OEBPS/nowhere.xhtml", spine[41], spine[1]};

  const std::vector<int16_t> got = resolveTocToSpine(cacheDir_, spine, toc, kIndexDoesNotFit);
  const std::vector<int16_t> expected = {5, 2, 63, 63, 0, 40, -1, 41, 1};
  EXPECT_EQ(got, expected);
}

TEST_F(BookMetadataCacheTest, ChunkedNothrowArrayInitializesAndIndexesAcrossChunkBoundaries) {
  ChunkedNothrowArray<int16_t, 256> values;
  ASSERT_TRUE(values.reset(600, static_cast<int16_t>(-1)));
  ASSERT_EQ(values.size(), 600u);
  EXPECT_EQ(values[0], -1);
  EXPECT_EQ(values[255], -1);
  EXPECT_EQ(values[256], -1);
  EXPECT_EQ(values[599], -1);
  values[255] = 12;
  values[256] = 34;
  EXPECT_EQ(values[255], 12);
  EXPECT_EQ(values[256], 34);
}

TEST_F(BookMetadataCacheTest, ChunkedNothrowArrayReportsAllocationRefusalWithoutPartialState) {
  ChunkedNothrowArray<int16_t, 256> values;
  {
    AllocationRefusal refusal(512);
    EXPECT_FALSE(values.reset(600, static_cast<int16_t>(-1)));
  }
  EXPECT_EQ(values.size(), 0u);
  EXPECT_TRUE(values.reset(3, static_cast<int16_t>(-1)));
  EXPECT_EQ(values.size(), 3u);
  EXPECT_EQ(values[0], -1);
}

TEST_F(BookMetadataCacheTest, BuildBookBinReturnsCleanlyOnMapOomAndRetriesTheSameBook) {
  const std::vector<std::string> hrefs = chapterHrefs(256);
  const std::string epubPath = writeSpineArchive(cacheDir_, hrefs);
  ASSERT_TRUE(fs::exists(epubPath));

  BookMetadataCache cache(cacheDir_);
  ASSERT_TRUE(prepareSpineAndToc(cache, hrefs));
  BookMetadataCache::BookMetadata metadata;
  metadata.title = "Allocation refusal integration";

  // 32 KiB free forces the existing batch-size optimization off. Refusing 512-byte
  // allocations then targets a single ChunkedNothrowArray data block in buildBookBin.
  {
    const HostFreeHeap lowHeap(32 * 1024);
    const AllocationRefusal refuseChunk(512, /*nothrowOnly=*/true);
    EXPECT_FALSE(cache.buildBookBin(epubPath, metadata));
  }

  ASSERT_TRUE(cache.buildBookBin(epubPath, metadata)) << "a failed cache build must leave the cache reusable";
  BookMetadataCache loaded(cacheDir_);
  ASSERT_TRUE(loaded.load());
  ASSERT_EQ(loaded.getSpineCount(), 256);
  ASSERT_EQ(loaded.getTocCount(), 256);
  EXPECT_EQ(loaded.getSpineEntry(0).tocIndex, 0);
  EXPECT_EQ(loaded.getSpineEntry(255).tocIndex, 255);
  EXPECT_TRUE(cache.cleanupTmpFiles());
}

TEST_F(BookMetadataCacheTest, BuildBookBinRejectsMissingZipAndRetriesAfterSourceRepair) {
  const std::vector<std::string> hrefs = chapterHrefs(32);
  const std::string epubPath = (fs::path(cacheDir_) / "book.epub").string();

  BookMetadataCache cache(cacheDir_);
  ASSERT_TRUE(prepareSpineAndToc(cache, hrefs));
  BookMetadataCache::BookMetadata metadata;
  metadata.title = "Missing ZIP retry";

  EXPECT_FALSE(cache.buildBookBin(epubPath, metadata));

  // Restoring the source must allow the same cache object to retry.
  writeSpineArchive(cacheDir_, hrefs);
  ASSERT_TRUE(cache.buildBookBin(epubPath, metadata));
  BookMetadataCache loaded(cacheDir_);
  ASSERT_TRUE(loaded.load());
  ASSERT_EQ(loaded.getSpineCount(), 32);
  EXPECT_EQ(loaded.getSpineEntry(0).tocIndex, 0);
  EXPECT_EQ(loaded.getSpineEntry(31).tocIndex, 31);
  EXPECT_TRUE(cache.cleanupTmpFiles());
}

}  // namespace
