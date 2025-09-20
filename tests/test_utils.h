#pragma once

/**
 * @file test_utils.h
 * @brief Testing utilities and macros
 */

#include <stdio.h>
#include <stdbool.h>

// Test result counters
extern int tests_run;
extern int tests_passed;
extern int tests_failed;

// Test macros
#define TEST_ASSERT(condition, message) \
    do { \
        tests_run++; \
        if (condition) { \
            tests_passed++; \
            printf("PASS: %s\n", message); \
        } else { \
            tests_failed++; \
            printf("FAIL: %s\n", message); \
        } \
    } while(0)

#define TEST_ASSERT_EQUAL_STR(expected, actual, message) \
    do { \
        tests_run++; \
        if (strcmp((expected), (actual)) == 0) { \
            tests_passed++; \
            printf("PASS: %s\n", message); \
        } else { \
            tests_failed++; \
            printf("FAIL: %s - Expected '%s', got '%s'\n", message, expected, actual); \
        } \
    } while(0)

#define TEST_ASSERT_EQUAL_INT(expected, actual, message) \
    do { \
        tests_run++; \
        if ((expected) == (actual)) { \
            tests_passed++; \
            printf("PASS: %s\n", message); \
        } else { \
            tests_failed++; \
            printf("FAIL: %s - Expected %ld, got %ld\n", message, (long)expected, (long)actual); \
        } \
    } while(0)

#define TEST_ASSERT_NULL(ptr, message) \
    TEST_ASSERT((ptr) == NULL, message)

#define TEST_ASSERT_NOT_NULL(ptr, message) \
    TEST_ASSERT((ptr) != NULL, message)

// Test framework functions
void test_init(const char *test_suite_name);
int test_finish(void);
void test_section(const char *section_name);