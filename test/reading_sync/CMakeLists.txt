set(ARDUINOJSON_DIR "${REPO_ROOT}/.pio/libdeps/micromarkd-x4pro/ArduinoJson/src" CACHE PATH "ArduinoJson headers")

add_executable(ReadingSyncTest
  ReadingSyncTest.cpp
  ${REPO_ROOT}/lib/ReadingSync/ReadingSync.cpp
)

target_include_directories(ReadingSyncTest PRIVATE
  ${REPO_ROOT}/lib/ReadingSync
  ${ARDUINOJSON_DIR}
)

target_link_libraries(ReadingSyncTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReadingSyncTest)
