cmake_minimum_required(VERSION 3.12)

project(wterm
    VERSION 2.0.0
    DESCRIPTION "WiFi Terminal Management Tool"
    LANGUAGES C
)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Build options
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_DOCS "Build documentation" OFF)
option(ENABLE_COVERAGE "Enable code coverage" OFF)
option(ENABLE_SANITIZERS "Enable sanitizers for debugging" OFF)

# Set default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic -Wformat=2 -Wstrict-prototypes")
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")

# Sanitizers for debugging
if(ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fsanitize=address,undefined -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address,undefined")
endif()

# Code coverage
if(ENABLE_COVERAGE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} --coverage")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} --coverage")
endif()

# No external UI libraries needed - using fzf

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Source files for string utilities library
set(STRING_UTILS_SOURCES
    src/utils/string_utils.c
    src/utils/input_sanitizer.c
)

# Source files for network scanner library
set(NETWORK_SCANNER_SOURCES
    src/core/network_scanner.c
    src/core/network_backends/backend_manager.c
    src/core/network_backends/nmcli_backend.c
)

# Source files for connection management library
set(CONNECTION_SOURCES
    src/core/connection.c
    src/core/error_handler.c
    src/core/hotspot_manager.c
    src/core/hotspot_ui.c
)

# Source files for fzf interface
set(FZF_UI_SOURCES
    src/fzf_ui.c
)

# Create string utilities library
add_library(wterm_string_utils STATIC ${STRING_UTILS_SOURCES})
target_include_directories(wterm_string_utils
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        src/utils
)

# Create network scanner library
add_library(wterm_network_scanner STATIC ${NETWORK_SCANNER_SOURCES})
target_include_directories(wterm_network_scanner
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        src/core
)
target_link_libraries(wterm_network_scanner wterm_string_utils)

# Create connection management library
add_library(wterm_connection STATIC ${CONNECTION_SOURCES})
target_include_directories(wterm_connection
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        src/core
)
target_link_libraries(wterm_connection wterm_string_utils)

# Create fzf UI library
add_library(wterm_fzf_ui STATIC ${FZF_UI_SOURCES})
target_include_directories(wterm_fzf_ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        src
)
target_link_libraries(wterm_fzf_ui
    wterm_string_utils
    wterm_network_scanner
    wterm_connection
)

# Create main executable
add_executable(wterm src/main.c)
set(WTERM_LIBRARIES
    wterm_network_scanner
    wterm_connection
    wterm_string_utils
    wterm_fzf_ui
)

target_link_libraries(wterm ${WTERM_LIBRARIES})

# Set executable properties
set_target_properties(wterm PROPERTIES
    OUTPUT_NAME wterm
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Installation rules
install(TARGETS wterm
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# Testing
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)

    # Include CI targets for local testing
    include(cmake/CITargets.cmake)
endif()

# Documentation
if(BUILD_DOCS)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/docs)
        doxygen_add_docs(docs
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            COMMENT "Generating documentation"
        )
    endif()
endif()

# Package configuration
set(CPACK_PACKAGE_NAME "wterm")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
set(CPACK_PACKAGE_CONTACT "Your Name <your.email@example.com>")
set(CPACK_GENERATOR "TGZ;DEB")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "network-manager")

include(CPack)

# Print configuration summary
message(STATUS "")
message(STATUS "wterm ${PROJECT_VERSION} Configuration Summary:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Build tests: ${BUILD_TESTS}")
message(STATUS "  Build docs: ${BUILD_DOCS}")
message(STATUS "  Enable coverage: ${ENABLE_COVERAGE}")
message(STATUS "  Enable sanitizers: ${ENABLE_SANITIZERS}")
message(STATUS "  Using fzf interface: ON")
message(STATUS "")
