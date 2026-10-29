add_executable(NoteMarkdownTest NoteMarkdownTest.cpp ../../src/activities/notes/NoteMarkdown.cpp)

target_include_directories(NoteMarkdownTest PRIVATE ../../src)

target_link_libraries(NoteMarkdownTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(NoteMarkdownTest)
