add_executable(kairos_tests
    test_config.cpp
    test_rolling_window.cpp
    test_logger.cpp
    test_cycle_queue.cpp
)
target_link_libraries(kairos_tests
    PRIVATE kairos_lib GTest::gtest_main
)
include(GoogleTest)
gtest_discover_tests(kairos_tests)
