add_executable(HighlightFormatTest HighlightFormatTest.cpp ../../src/activities/reader/HighlightFormat.cpp)

target_include_directories(HighlightFormatTest PRIVATE ../../src)

target_link_libraries(HighlightFormatTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HighlightFormatTest)
