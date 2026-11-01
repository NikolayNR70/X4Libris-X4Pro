add_executable(Fb2ConverterHost
  main.cpp
  UzlibChecksumStubs.cpp
  ${REPO_ROOT}/lib/Fb2Converter/Fb2Converter.cpp
  ${REPO_ROOT}/lib/SaxParser/SaxParserYxml.cpp
  ${REPO_ROOT}/lib/SaxParser/yxml.c
  ${REPO_ROOT}/lib/ZipFile/ZipFile.cpp
)
target_include_directories(Fb2ConverterHost PRIVATE
  stubs
  ${REPO_ROOT}/lib/Fb2Converter
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/SaxParser
  ${REPO_ROOT}/lib/ZipFile
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
)
target_link_libraries(Fb2ConverterHost PRIVATE crosspoint_test_common inflate_reader_host)
add_test(NAME Fb2ConverterPublication
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_tests.py $<TARGET_FILE:Fb2ConverterHost>
)
add_test(NAME Fb2AnnotationMetadata
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/annotation_verify.py $<TARGET_FILE:Fb2ConverterHost>
)
