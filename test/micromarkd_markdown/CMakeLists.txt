add_executable(MicroMarkDMarkdownTest
  MarkdownLineParserTest.cpp
  MarkdownDocumentTest.cpp
  MarkdownRecoveryPlanTest.cpp
  ${REPO_ROOT}/lib/MicroMarkDMarkdown/MarkdownLineParser.cpp
  ${REPO_ROOT}/lib/MicroMarkDMarkdown/MarkdownDocument.cpp
  ${REPO_ROOT}/lib/MicroMarkDMarkdown/MarkdownRecoveryPlan.cpp
)

target_include_directories(MicroMarkDMarkdownTest PRIVATE
  ${REPO_ROOT}/lib
)

target_link_libraries(MicroMarkDMarkdownTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(MicroMarkDMarkdownTest)
