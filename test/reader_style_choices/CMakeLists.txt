add_executable(ReaderStyleChoicesTest ReaderStyleChoicesTest.cpp)
add_dependencies(ReaderStyleChoicesTest i18n_generated)

target_include_directories(ReaderStyleChoicesTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/I18n
  ${I18N_GENERATED_DIR}
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/test/epub_pipeline
  ${REPO_ROOT}/test/zip_entry_reader
  ${REPO_ROOT}/test/shims
)

target_link_libraries(ReaderStyleChoicesTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReaderStyleChoicesTest)
