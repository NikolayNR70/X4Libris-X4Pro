add_executable(ApplicationsTest
  ApplicationsTest.cpp
  ${REPO_ROOT}/src/activities/notes/NotesCore.cpp
)
target_include_directories(ApplicationsTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(ApplicationsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ApplicationsTest)
