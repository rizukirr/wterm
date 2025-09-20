/**
 * @file test_utils.c
 * @brief Testing utilities implementation
 */

#include "test_utils.h"
#include <string.h>

// Global test counters
int tests_run = 0;
int tests_passed = 0;
int tests_failed = 0;

void test_init(const char *test_suite_name) {
    tests_run = 0;
    tests_passed = 0;
    tests_failed = 0;

    printf("Running test suite: %s\n", test_suite_name);
    printf("=" );
    for (size_t i = 0; i < strlen(test_suite_name) + 20; i++) {
        printf("=");
    }
    printf("\n\n");
}

int test_finish(void) {
    printf("\nTest Results:\n");
    printf("=============\n");
    printf("Total tests: %d\n", tests_run);
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    if (tests_failed == 0) {
        printf("\nAll tests passed! ✓\n");
        return 0;
    } else {
        printf("\n%d test(s) failed! ✗\n", tests_failed);
        return 1;
    }
}

void test_section(const char *section_name) {
    printf("\n--- %s ---\n", section_name);
}