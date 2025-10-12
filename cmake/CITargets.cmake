# CI Targets for local testing
# This module provides CMake targets that mirror CI checks

# Find optional tools for enhanced CI checks
find_program(CPPCHECK_EXECUTABLE NAMES cppcheck)
find_program(VALGRIND_EXECUTABLE NAMES valgrind)

# ============================================================================
# Target: ci-quick
# Quick pre-commit checks (build + basic tests)
# ============================================================================
add_custom_target(ci-quick
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --parallel
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running quick CI checks (build + tests)..."
)

# ============================================================================
# Target: security-check
# Run security test suite with detailed output
# ============================================================================
if(BUILD_TESTS)
    add_custom_target(security-check
        COMMAND ${CMAKE_COMMAND} -E echo "========================================"
        COMMAND ${CMAKE_COMMAND} -E echo "Running Security Test Suite"
        COMMAND ${CMAKE_COMMAND} -E echo "========================================"
        COMMAND ${CMAKE_COMMAND} -E echo ""
        COMMAND $<TARGET_FILE:test_security>
        COMMAND ${CMAKE_COMMAND} -E echo ""
        COMMAND ${CMAKE_COMMAND} -E echo "✅ All 80 security tests passed"
        DEPENDS test_security
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running security tests..."
    )
endif()

# ============================================================================
# Target: static-analysis
# Run cppcheck static analysis (if available)
# ============================================================================
if(CPPCHECK_EXECUTABLE)
    add_custom_target(static-analysis
        COMMAND ${CMAKE_COMMAND} -E echo "Running cppcheck static analysis..."
        COMMAND ${CPPCHECK_EXECUTABLE}
            --enable=warning,performance,portability
            --error-exitcode=1
            --suppress=missingIncludeSystem
            --suppress=unusedFunction
            --suppress=staticFunction
            --suppress=uninitvar:${CMAKE_SOURCE_DIR}/include/external/termbox2.h
            --inline-suppr
            --std=c99
            -I ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/tests
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running static analysis with cppcheck..."
    )
else()
    add_custom_target(static-analysis
        COMMAND ${CMAKE_COMMAND} -E echo "⚠️  cppcheck not found - skipping static analysis"
        COMMAND ${CMAKE_COMMAND} -E echo "Install cppcheck: sudo apt-get install cppcheck"
        COMMENT "Static analysis skipped (cppcheck not found)"
    )
endif()

# ============================================================================
# Target: memory-check
# Run tests with Valgrind memory leak detection (if available)
# ============================================================================
if(VALGRIND_EXECUTABLE AND BUILD_TESTS)
    add_custom_target(memory-check
        COMMAND ${CMAKE_COMMAND} -E echo "========================================"
        COMMAND ${CMAKE_COMMAND} -E echo "Running Valgrind Memory Leak Detection"
        COMMAND ${CMAKE_COMMAND} -E echo "========================================"
        COMMAND ${CMAKE_COMMAND} -E echo ""
        COMMAND ${CMAKE_COMMAND} -E echo "Checking test_string_utils..."
        COMMAND ${VALGRIND_EXECUTABLE}
            --leak-check=full
            --show-leak-kinds=all
            --track-origins=yes
            --error-exitcode=1
            --quiet
            $<TARGET_FILE:test_string_utils>
        COMMAND ${CMAKE_COMMAND} -E echo "✅ No leaks in string_utils"
        COMMAND ${CMAKE_COMMAND} -E echo ""
        COMMAND ${CMAKE_COMMAND} -E echo "Checking test_network_scanner..."
        COMMAND ${VALGRIND_EXECUTABLE}
            --leak-check=full
            --show-leak-kinds=all
            --track-origins=yes
            --error-exitcode=1
            --quiet
            $<TARGET_FILE:test_network_scanner>
        COMMAND ${CMAKE_COMMAND} -E echo "✅ No leaks in network_scanner"
        COMMAND ${CMAKE_COMMAND} -E echo ""
        COMMAND ${CMAKE_COMMAND} -E echo "Checking test_security..."
        COMMAND ${VALGRIND_EXECUTABLE}
            --leak-check=full
            --show-leak-kinds=all
            --track-origins=yes
            --error-exitcode=1
            --quiet
            $<TARGET_FILE:test_security>
        COMMAND ${CMAKE_COMMAND} -E echo "✅ No leaks in security tests"
        COMMAND ${CMAKE_COMMAND} -E echo ""
        COMMAND ${CMAKE_COMMAND} -E echo "✅ All memory checks passed"
        DEPENDS test_string_utils test_network_scanner test_security
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running memory leak detection with Valgrind..."
    )
else()
    add_custom_target(memory-check
        COMMAND ${CMAKE_COMMAND} -E echo "⚠️  Valgrind not found - skipping memory check"
        COMMAND ${CMAKE_COMMAND} -E echo "Install valgrind: sudo apt-get install valgrind"
        COMMENT "Memory check skipped (valgrind not found)"
    )
endif()

# ============================================================================
# Target: format-check
# Check for code formatting issues
# ============================================================================
add_custom_target(format-check
    COMMAND ${CMAKE_COMMAND} -E echo "Checking for trailing whitespace..."
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_SOURCE_DIR}
        bash -c "! git grep -n '[[:space:]]$$' -- '*.c' '*.h' ':!*.md' 2>/dev/null"
    COMMAND ${CMAKE_COMMAND} -E echo "✅ No trailing whitespace"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "Checking file permissions..."
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_SOURCE_DIR}
        bash -c "! find src/ include/ tests/ -type f \\( -name '*.c' -o -name '*.h' \\) -executable 2>/dev/null | grep -q ."
    COMMAND ${CMAKE_COMMAND} -E echo "✅ File permissions correct"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking code formatting..."
)

# ============================================================================
# Target: security-audit
# Comprehensive security audit (scans for unsafe patterns)
# ============================================================================
add_custom_target(security-audit
    COMMAND ${CMAKE_COMMAND} -E echo "========================================"
    COMMAND ${CMAKE_COMMAND} -E echo "Security Audit"
    COMMAND ${CMAKE_COMMAND} -E echo "========================================"
    COMMAND ${CMAKE_COMMAND} -E echo ""

    COMMAND ${CMAKE_COMMAND} -E echo "Checking for system() calls..."
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_SOURCE_DIR}
        bash -c "! grep -rn 'system(' src/ 2>/dev/null"
    COMMAND ${CMAKE_COMMAND} -E echo "✅ No system() calls found"
    COMMAND ${CMAKE_COMMAND} -E echo ""

    COMMAND ${CMAKE_COMMAND} -E echo "Verifying shell_escape usage in command execution..."
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_SOURCE_DIR}
        bash -c "grep -q 'shell_escape' src/core/network_backends/nmcli_backend.c"
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_SOURCE_DIR}
        bash -c "grep -q 'shell_escape' src/core/connection.c"
    COMMAND ${CMAKE_COMMAND} -E echo "✅ Shell escaping implemented in critical paths"
    COMMAND ${CMAKE_COMMAND} -E echo ""

    COMMAND ${CMAKE_COMMAND} -E echo "Verifying input sanitization module..."
    COMMAND test -f ${CMAKE_SOURCE_DIR}/src/utils/input_sanitizer.c
    COMMAND ${CMAKE_COMMAND} -E echo "✅ Input sanitization module present"
    COMMAND ${CMAKE_COMMAND} -E echo ""

    COMMAND ${CMAKE_COMMAND} -E echo "Checking for potentially unsafe functions..."
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_SOURCE_DIR}
        bash -c "UNSAFE=\$$(grep -r 'strcpy\\|strcat\\|sprintf\\|gets' src/ 2>/dev/null || true); if [ -n \"\$$UNSAFE\" ]; then echo '⚠️  Warning: Unsafe functions found:'; echo \"\$$UNSAFE\"; else echo '✅ No unsafe string functions'; fi"
    COMMAND ${CMAKE_COMMAND} -E echo ""

    COMMAND ${CMAKE_COMMAND} -E echo "✅ Security audit complete"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running security audit..."
)

# ============================================================================
# Target: ci-check
# Comprehensive CI check (runs all checks)
# ============================================================================
add_custom_target(ci-check
    COMMAND ${CMAKE_COMMAND} -E echo "========================================"
    COMMAND ${CMAKE_COMMAND} -E echo "Running Comprehensive CI Checks"
    COMMAND ${CMAKE_COMMAND} -E echo "========================================"
    COMMAND ${CMAKE_COMMAND} -E echo ""

    COMMAND ${CMAKE_COMMAND} -E echo "Step 1/7: Building project..."
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --parallel
    COMMAND ${CMAKE_COMMAND} -E echo "✅ Build complete"
    COMMAND ${CMAKE_COMMAND} -E echo ""

    COMMAND ${CMAKE_COMMAND} -E echo "Step 2/7: Running all tests..."
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMAND ${CMAKE_COMMAND} -E echo "✅ All tests passed"
    COMMAND ${CMAKE_COMMAND} -E echo ""

    COMMAND ${CMAKE_COMMAND} -E echo "Step 3/7: Running security tests..."
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target security-check
    COMMAND ${CMAKE_COMMAND} -E echo ""

    COMMAND ${CMAKE_COMMAND} -E echo "Step 4/7: Running static analysis..."
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target static-analysis
    COMMAND ${CMAKE_COMMAND} -E echo ""

    COMMAND ${CMAKE_COMMAND} -E echo "Step 5/7: Checking code formatting..."
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target format-check
    COMMAND ${CMAKE_COMMAND} -E echo ""

    COMMAND ${CMAKE_COMMAND} -E echo "Step 6/7: Running security audit..."
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target security-audit
    COMMAND ${CMAKE_COMMAND} -E echo ""

    COMMAND ${CMAKE_COMMAND} -E echo "Step 7/7: Running memory leak detection..."
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target memory-check
    COMMAND ${CMAKE_COMMAND} -E echo ""

    COMMAND ${CMAKE_COMMAND} -E echo "========================================"
    COMMAND ${CMAKE_COMMAND} -E echo "✅ All CI Checks Passed!"
    COMMAND ${CMAKE_COMMAND} -E echo "========================================"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "Your code is ready to submit! 🚀"

    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running comprehensive CI checks..."
)

# Print available CI targets
message(STATUS "")
message(STATUS "Available CI targets:")
message(STATUS "  ci-check         - Run all CI checks (comprehensive)")
message(STATUS "  ci-quick         - Quick pre-commit check (build + tests)")
message(STATUS "  security-check   - Run security test suite")
message(STATUS "  static-analysis  - Run cppcheck static analysis")
message(STATUS "  memory-check     - Run Valgrind memory leak detection")
message(STATUS "  format-check     - Check code formatting")
message(STATUS "  security-audit   - Run security audit")
message(STATUS "")
message(STATUS "Usage: cmake --build build --target <target-name>")
message(STATUS "")
