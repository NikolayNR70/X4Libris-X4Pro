add_executable(KOReaderCachePathTest
  KOReaderCachePathSmoke.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/FsHelpers/NaturalSort.cpp
)

target_include_directories(KOReaderCachePathTest PRIVATE
  ${REPO_ROOT}/test/shims
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/KOReaderSync
)

target_link_libraries(KOReaderCachePathTest PRIVATE crosspoint_test_common)

add_test(NAME KOReaderCacheDirectoryPath COMMAND KOReaderCachePathTest)
