# Unity Test Framework
set(UNITY_SOURCES unity/unity.c)
set(UNITY_HEADERS unity/unity.h unity/unity_internals.h)

# Test sources
set(TEST_SOURCES
    test_fixture.c
    main.c
    test_utils.c
    test_bitstream.c
    test_reader.c
    test_dsl.c
)

# Create test executable
add_executable(lsd_tests
    ${UNITY_SOURCES}
    ${TEST_SOURCES}
)

target_include_directories(lsd_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/unity
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(lsd_tests PRIVATE lsd)

# Enable testing
add_test(NAME UnityTestSuite COMMAND lsd_tests)
