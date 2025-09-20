# Tests CMakeLists.txt

# Test utilities library
set(TEST_UTILS_SOURCES
    test_utils.c
)

add_library(test_utils STATIC ${TEST_UTILS_SOURCES})
target_include_directories(test_utils
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

# String utilities tests
add_executable(test_string_utils test_string_utils.c)
target_link_libraries(test_string_utils
    wterm_string_utils
    test_utils
)

add_test(NAME string_utils_test
         COMMAND test_string_utils
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# Network scanner tests
add_executable(test_network_scanner test_network_scanner.c)
target_link_libraries(test_network_scanner
    wterm_network_scanner
    wterm_string_utils
    test_utils
)

add_test(NAME network_scanner_test
         COMMAND test_network_scanner
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# Integration tests
add_executable(test_integration test_integration.c)
target_link_libraries(test_integration
    wterm_network_scanner
    wterm_string_utils
    test_utils
)

add_test(NAME integration_test
         COMMAND test_integration
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# Set test properties
set_tests_properties(string_utils_test network_scanner_test integration_test
    PROPERTIES
        TIMEOUT 30
        LABELS "unit"
)