#include <InflateReader.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>
#include <vector>

#include "BuildArena.h"
#include "HashUtils.h"
#include "StoredZipWriter.h"
#include "ZipFile.h"

static_assert(!std::is_constructible_v<ZipFile, std::string&&>,
              "ZipFile borrows the archive path and must reject temporary strings");

// Path to moby-dick.epub injected by CMake via FIXTURE_EPUB define.
#ifndef FIXTURE_EPUB
#error "FIXTURE_EPUB must be defined (path to test/fixtures/moby-dick.epub)"
#endif
#ifndef JPEG_CRC_FIXTURE
#error "JPEG_CRC_FIXTURE must be defined (path to a deflated image fixture)"
#endif

// A deflated spine chapter from moby-dick.epub (confirmed present).
static const char* const kTestEntry = "OEBPS/3308270851576161417_2701-h-0.htm.html";

class ZipEntryReaderTest : public ::testing::Test {
 protected:
  std::string epubPath_{FIXTURE_EPUB};
  ZipFile zip_{epubPath_};
};

// Helper: drain an EntryReader into a byte vector.
static bool drain(ZipFile::EntryReader& reader, std::vector<uint8_t>& out) {
  uint8_t buf[512];
  bool done = false;
  while (!done) {
    size_t produced = 0;
    if (!reader.step(buf, sizeof(buf), &produced, &done)) return false;
    out.insert(out.end(), buf, buf + produced);
  }
  return true;
}

static uint32_t crc32(const std::vector<uint8_t>& bytes) {
  uint32_t crc = 0xffffffffu;
  for (uint8_t byte : bytes) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

static bool corruptCentralDirectoryCrc(const std::filesystem::path& path, const char* entryName) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return false;
  std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(input), {});
  const size_t nameSize = std::strlen(entryName);
  for (size_t i = 0; i + 46 + nameSize <= bytes.size(); ++i) {
    if (bytes[i] != 0x50 || bytes[i + 1] != 0x4b || bytes[i + 2] != 0x01 || bytes[i + 3] != 0x02) continue;
    const size_t nameLen = bytes[i + 28] | (static_cast<size_t>(bytes[i + 29]) << 8);
    if (nameLen != nameSize || std::memcmp(bytes.data() + i + 46, entryName, nameSize) != 0) continue;
    bytes[i + 16] ^= 0x01;  // Alter expected CRC while preserving valid compressed data.
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(output);
  }
  return false;
}

// Helper: drain readFileToStream into a byte vector via a Print sink.
class VectorSink : public Print {
 public:
  std::vector<uint8_t> data;
  size_t write(uint8_t b) override {
    data.push_back(b);
    return 1;
  }
  size_t write(const uint8_t* buf, size_t n) override {
    data.insert(data.end(), buf, buf + n);
    return n;
  }
};

// Chunked EntryReader output matches one-shot readFileToStream.
TEST_F(ZipEntryReaderTest, ChunkedOutputMatchesOneShot) {
  // Reference: one-shot readFileToStream
  VectorSink ref;
  ASSERT_TRUE(zip_.readFileToStream(kTestEntry, ref, 1024));
  ASSERT_GT(ref.data.size(), 0u);

  // Under test: EntryReader in small 97-byte chunks (prime, stresses boundary)
  ZipFile::EntryReader reader(zip_);
  ASSERT_TRUE(reader.open(kTestEntry));
  EXPECT_TRUE(reader.isOpen());
  EXPECT_EQ(reader.inflatedSize(), ref.data.size());

  std::vector<uint8_t> got;
  ASSERT_TRUE(drain(reader, got));

  EXPECT_EQ(got.size(), ref.data.size());
  EXPECT_EQ(got, ref.data);
  EXPECT_EQ(reader.bytesProduced(), ref.data.size());
}

TEST_F(ZipEntryReaderTest, CentralDirectoryExposesEntryCrc) {
  ZipFile::FileStatSlim stat{};
  ASSERT_TRUE(zip_.loadFileStatSlim(kTestEntry, &stat));
  VectorSink contents;
  ASSERT_TRUE(zip_.readFileToStream(kTestEntry, contents, 1024));
  EXPECT_EQ(stat.crc32, crc32(contents.data));
}

TEST_F(ZipEntryReaderTest, FullReadRejectsStoredEntryWhoseBytesDoNotMatchItsCrc) {
  const auto path = std::filesystem::temp_directory_path() / "x4libris_crc_mismatch.epub";
  const std::string archivePath = path.string();
  test_zip::StoredZipWriter writer;
  writer.add("payload.bin", "stored payload with integrity protection");
  writer.write(archivePath);

  ZipFile source(archivePath);
  uint32_t dataOffset = 0;
  uint32_t dataSize = 0;
  ASSERT_TRUE(source.getStoredEntryRange("payload.bin", &dataOffset, &dataSize));
  ASSERT_GT(dataSize, 0u);
  {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(file);
    file.seekg(dataOffset);
    char byte = 0;
    file.read(&byte, 1);
    ASSERT_TRUE(file);
    byte ^= 1;
    file.seekp(dataOffset);
    file.write(&byte, 1);
    ASSERT_TRUE(file);
  }

  ZipFile corrupted(archivePath);
  size_t size = 0;
  uint8_t* contents = corrupted.readFileToMemory("payload.bin", &size);
  EXPECT_EQ(contents, nullptr) << "full reads must reject bytes that disagree with ZIP CRC-32";
  free(contents);
  VectorSink sink;
  EXPECT_FALSE(corrupted.readFileToStream("payload.bin", sink, 16));
  uint8_t storedProbe[64] = {};
  EXPECT_EQ(corrupted.readBytesFromEntry("payload.bin", storedProbe, sizeof(storedProbe)), 0u);
  ZipFile::EntryReader reader(corrupted, 16);
  ASSERT_TRUE(reader.open("payload.bin"));
  std::vector<uint8_t> drained;
  EXPECT_FALSE(drain(reader, drained));
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST_F(ZipEntryReaderTest, FullDeflatedReadRejectsWrongCentralDirectoryCrc) {
  const auto path = std::filesystem::temp_directory_path() / "x4libris_deflated_crc_mismatch.epub";
  std::error_code ec;
  std::filesystem::copy_file(epubPath_, path, std::filesystem::copy_options::overwrite_existing, ec);
  ASSERT_FALSE(ec);
  ASSERT_TRUE(corruptCentralDirectoryCrc(path, kTestEntry));
  const std::string archivePath = path.string();
  ZipFile corrupted(archivePath);
  ZipFile::FileStatSlim stat{};
  ASSERT_TRUE(corrupted.loadFileStatSlim(kTestEntry, &stat));
  ASSERT_EQ(stat.method, 8u);

  size_t size = 0;
  uint8_t* contents = corrupted.readFileToMemory(kTestEntry, &size);
  EXPECT_EQ(contents, nullptr);
  free(contents);
  VectorSink sink;
  EXPECT_FALSE(corrupted.readFileToStream(kTestEntry, sink, 512));
  std::vector<uint8_t> fullProbe(stat.uncompressedSize);
  EXPECT_EQ(corrupted.readBytesFromStat(stat, fullProbe.data(), fullProbe.size()), 0u);
  ZipFile::EntryReader reader(corrupted, 512);
  ASSERT_TRUE(reader.open(stat));
  std::vector<uint8_t> drained;
  EXPECT_FALSE(drain(reader, drained));
  std::filesystem::remove(path, ec);
}

TEST_F(ZipEntryReaderTest, FullDeflatedReadsAcceptValidLargeBinaryEntry) {
  const std::string archivePath = JPEG_CRC_FIXTURE;
  ZipFile zip(archivePath);
  constexpr const char* kImage = "OEBPS/images/photo.jpg";
  ZipFile::FileStatSlim stat{};
  ASSERT_TRUE(zip.loadFileStatSlim(kImage, &stat));
  ASSERT_EQ(stat.method, 8u);
  ASSERT_GT(stat.uncompressedSize, 4096u);

  VectorSink streamed;
  ASSERT_TRUE(zip.readFileToStream(kImage, streamed, 512));
  EXPECT_EQ(streamed.data.size(), stat.uncompressedSize);
  EXPECT_EQ(crc32(streamed.data), stat.crc32);
  std::vector<uint8_t> fullProbe(stat.uncompressedSize);
  EXPECT_EQ(zip.readBytesFromStat(stat, fullProbe.data(), fullProbe.size()), stat.uncompressedSize);
  EXPECT_EQ(crc32(fullProbe), stat.crc32);

  ZipFile::EntryReader reader(zip, 512);
  ASSERT_TRUE(reader.open(stat));
  std::vector<uint8_t> read;
  ASSERT_TRUE(drain(reader, read));
  EXPECT_EQ(read, streamed.data);
  EXPECT_EQ(crc32(read), stat.crc32);

  BuildArena arena(40 * 1024);
  ASSERT_TRUE(arena.valid());
  ZipFile::EntryReader arenaReader(zip, 512, &arena);
  ASSERT_TRUE(arenaReader.open(stat));
  std::vector<uint8_t> arenaRead;
  EXPECT_TRUE(drain(arenaReader, arenaRead));
  EXPECT_EQ(arenaRead, streamed.data);
  EXPECT_EQ(crc32(arenaRead), stat.crc32);
  arenaReader.close();
  EXPECT_EQ(arena.used(), 0u);
}

TEST_F(ZipEntryReaderTest, BatchStatsMatchSingleEntryLookup) {
  const size_t nameLen = std::strlen(kTestEntry);
  const std::deque<ZipFile::SizeTarget> targets = {
      {HashUtils::fnvHash64(kTestEntry, nameLen), static_cast<uint16_t>(nameLen), 0}};
  std::deque<ZipFile::FileStatSlim> batch(1);
  ASSERT_EQ(zip_.fillFileStats(targets, batch), 1);

  ZipFile::FileStatSlim single{};
  ASSERT_TRUE(zip_.loadFileStatSlim(kTestEntry, &single));
  EXPECT_EQ(batch[0].method, single.method);
  EXPECT_EQ(batch[0].crc32, single.crc32);
  EXPECT_EQ(batch[0].compressedSize, single.compressedSize);
  EXPECT_EQ(batch[0].uncompressedSize, single.uncompressedSize);
  EXPECT_EQ(batch[0].localHeaderOffset, single.localHeaderOffset);
}

// Single large step (cap > inflatedSize) still terminates correctly.
TEST_F(ZipEntryReaderTest, SingleLargeStep) {
  ZipFile::EntryReader reader(zip_);
  ASSERT_TRUE(reader.open(kTestEntry));

  std::vector<uint8_t> buf(reader.inflatedSize() + 64);
  size_t produced = 0;
  bool done = false;
  ASSERT_TRUE(reader.step(buf.data(), buf.size(), &produced, &done));
  // May finish in one step (Done) or need another (Ok with produced < cap).
  // Either way, draining must complete without error.
  std::vector<uint8_t> all(buf.data(), buf.data() + produced);
  while (!done) {
    ASSERT_TRUE(reader.step(buf.data(), buf.size(), &produced, &done));
    all.insert(all.end(), buf.data(), buf.data() + produced);
  }

  VectorSink ref;
  ASSERT_TRUE(zip_.readFileToStream(kTestEntry, ref, 1024));
  EXPECT_EQ(all, ref.data);
}

// Re-opening the same reader on a different entry works correctly.
TEST_F(ZipEntryReaderTest, ReOpen) {
  // Open the CSS file (a short stored or deflated entry).
  const char* kCssEntry = "OEBPS/pgepub.css";

  ZipFile::EntryReader reader(zip_);
  ASSERT_TRUE(reader.open(kTestEntry));
  std::vector<uint8_t> first;
  ASSERT_TRUE(drain(reader, first));

  ASSERT_TRUE(reader.open(kCssEntry));
  std::vector<uint8_t> css;
  ASSERT_TRUE(drain(reader, css));

  VectorSink cssRef;
  ASSERT_TRUE(zip_.readFileToStream(kCssEntry, cssRef, 1024));
  EXPECT_EQ(css, cssRef.data);

  // Re-open the first entry and confirm it still matches.
  ASSERT_TRUE(reader.open(kTestEntry));
  std::vector<uint8_t> again;
  ASSERT_TRUE(drain(reader, again));
  EXPECT_EQ(again, first);
}

// close() leaves the reader in a safe state; isOpen() reflects it.
TEST_F(ZipEntryReaderTest, CloseResetsState) {
  ZipFile::EntryReader reader(zip_);
  ASSERT_TRUE(reader.open(kTestEntry));
  EXPECT_TRUE(reader.isOpen());
  reader.close();
  EXPECT_FALSE(reader.isOpen());
  EXPECT_EQ(reader.bytesProduced(), 0u);
  EXPECT_EQ(reader.inflatedSize(), 0u);
}

// Arena-backed reader (readBuf + inflate ring carved from a BuildArena) must
// produce byte-identical output, reclaim its whole scope on close, and never
// touch malloc for the ring (plan Phase 2, EntryReader site).
// open(stat, cap): the first `cap` bytes exactly, then done -- from a ring sized to the cap, not
// to the entry. This is what lets a JPEG header walk over a 400 KB entry cost a 16 KB ring: a
// deflate back-reference never reaches further back than the bytes produced so far.
TEST_F(ZipEntryReaderTest, OutputCapStopsAtTheCapFromACapSizedRing) {
  VectorSink ref;
  ASSERT_TRUE(zip_.readFileToStream(kTestEntry, ref, 1024));
  constexpr size_t kCap = 3000;
  ASSERT_GT(ref.data.size(), 2 * kCap) << "the entry must be much larger than the cap";

  ZipFile::FileStatSlim stat = {};
  ASSERT_TRUE(zip_.loadFileStatSlim(kTestEntry, &stat));

  BuildArena arena(40 * 1024);
  ASSERT_TRUE(arena.valid());
  {
    ZipFile::EntryReader reader(zip_, 512, &arena);
    ASSERT_TRUE(reader.open(stat, kCap));
    // 512 B chunk + a ring for 3000 B of output (+ alignment): nowhere near the 32 KB ring.
    EXPECT_LE(arena.highWater(), 512u + InflateReader::ringSizeFor(kCap) + 64u);

    std::vector<uint8_t> got;
    ASSERT_TRUE(drain(reader, got));
    ASSERT_EQ(got.size(), kCap);
    EXPECT_TRUE(std::equal(got.begin(), got.end(), ref.data.begin())) << "the capped read is an exact prefix";
    EXPECT_EQ(reader.bytesProduced(), kCap);
    EXPECT_EQ(reader.inflatedSize(), ref.data.size()) << "the entry's real size is still reported";
  }
  EXPECT_EQ(arena.used(), 0u);

  // A cap past the end changes nothing.
  ZipFile::EntryReader whole(zip_, 512);
  ASSERT_TRUE(whole.open(stat, ref.data.size() * 2));
  std::vector<uint8_t> all;
  ASSERT_TRUE(drain(whole, all));
  EXPECT_EQ(all, ref.data);
}

TEST_F(ZipEntryReaderTest, ArenaBackedOutputMatchesAndReclaims) {
  VectorSink ref;
  ASSERT_TRUE(zip_.readFileToStream(kTestEntry, ref, 1024));
  ASSERT_GT(ref.data.size(), 0u);

  BuildArena arena(40 * 1024);  // 1 KB readBuf + <=32 KB ring + alignment
  ASSERT_TRUE(arena.valid());
  const size_t before = arena.used();
  {
    ZipFile::EntryReader reader(zip_, 1024, &arena);
    ASSERT_TRUE(reader.open(kTestEntry));
    EXPECT_GT(arena.used(), before) << "ring/readBuf should come from the arena";

    std::vector<uint8_t> got;
    ASSERT_TRUE(drain(reader, got));
    EXPECT_EQ(got, ref.data);
  }
  EXPECT_EQ(arena.used(), before) << "reader close must release its arena scope";
  EXPECT_EQ(arena.failedAllocSize(), 0u);
  EXPECT_LE(arena.highWater(), 34u * 1024u);
}

// Arena too small for the ring: open must fail cleanly and release the scope.
TEST_F(ZipEntryReaderTest, ArenaTooSmallFailsCleanly) {
  BuildArena arena(2 * 1024);  // fits readBuf, not the ring
  ASSERT_TRUE(arena.valid());
  ZipFile::EntryReader reader(zip_, 1024, &arena);
  EXPECT_FALSE(reader.open(kTestEntry));
  EXPECT_EQ(arena.used(), 0u);
  EXPECT_GT(arena.failedAllocSize(), 0u);
}

TEST_F(ZipEntryReaderTest, CloseCannotReleaseNewerCallerBlock) {
  BuildArena arena(40 * 1024);
  ZipFile::EntryReader reader(zip_, 1024, &arena);
  ASSERT_TRUE(reader.open(kTestEntry));
  const size_t readerUsed = arena.used();

  auto callerBlock = arena.reserveBlock();
  ASSERT_NE(arena.alloc(64, 1), nullptr);
  reader.close();
  EXPECT_GT(arena.used(), readerUsed);

  EXPECT_TRUE(arena.release(callerBlock));
  reader.close();
  EXPECT_EQ(arena.used(), 0u);
}
