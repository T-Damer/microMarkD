add_executable(MicroMarkDMarkdownTest
  MarkdownLineParserTest.cpp
  ${REPO_ROOT}/lib/MicroMarkDMarkdown/MarkdownLineParser.cpp
)

target_include_directories(MicroMarkDMarkdownTest PRIVATE
  ${REPO_ROOT}/lib
)

target_link_libraries(MicroMarkDMarkdownTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(MicroMarkDMarkdownTest)
