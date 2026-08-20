add_executable(reader_regressions_test
  BookmarkStoreTest.cpp
  RubyCjkLayoutTest.cpp
  TokenBoundaryTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  fakes/BookIdentityFake.cpp
)

target_include_directories(reader_regressions_test BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/fakes
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(reader_regressions_test PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(reader_regressions_test)
