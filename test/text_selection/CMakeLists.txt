add_executable(TextSelectionTest TextSelectionTest.cpp ../../src/components/typing/TextSelection.cpp)

target_include_directories(TextSelectionTest PRIVATE ../../src)

target_link_libraries(TextSelectionTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TextSelectionTest)
