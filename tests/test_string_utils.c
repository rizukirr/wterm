/**
 * @file test_string_utils.c
 * @brief Unit tests for string utilities
 */

#include "test_utils.h"
#include "../src/utils/string_utils.h"
#include <string.h>

static void test_safe_string_copy(void) {
    test_section("Testing safe_string_copy");

    char dest[10];
    bool result;

    // Test normal copy
    result = safe_string_copy(dest, "hello", sizeof(dest));
    TEST_ASSERT(result == true, "Normal copy should succeed");
    TEST_ASSERT_EQUAL_STR("hello", dest, "Normal copy content");

    // Test truncation
    result = safe_string_copy(dest, "this is too long", sizeof(dest));
    TEST_ASSERT(result == false, "Long string should indicate truncation");
    TEST_ASSERT_EQUAL_INT(9, strlen(dest), "Truncated string length");

    // Test null inputs
    result = safe_string_copy(NULL, "test", 10);
    TEST_ASSERT(result == false, "NULL dest should fail");

    result = safe_string_copy(dest, NULL, sizeof(dest));
    TEST_ASSERT(result == false, "NULL src should fail");

    result = safe_string_copy(dest, "test", 0);
    TEST_ASSERT(result == false, "Zero size should fail");
}

static void test_trim_functions(void) {
    test_section("Testing trim functions");

    char test_str[50];

    // Test trailing whitespace trimming
    strcpy(test_str, "hello   ");
    trim_trailing_whitespace(test_str);
    TEST_ASSERT_EQUAL_STR("hello", test_str, "Trailing whitespace removal");

    strcpy(test_str, "hello\t\n ");
    trim_trailing_whitespace(test_str);
    TEST_ASSERT_EQUAL_STR("hello", test_str, "Mixed trailing whitespace removal");

    // Test leading whitespace trimming
    const char *result = trim_leading_whitespace("   hello");
    TEST_ASSERT_EQUAL_STR("hello", result, "Leading whitespace removal");

    result = trim_leading_whitespace("\t\n hello");
    TEST_ASSERT_EQUAL_STR("hello", result, "Mixed leading whitespace removal");

    result = trim_leading_whitespace("hello");
    TEST_ASSERT_EQUAL_STR("hello", result, "No leading whitespace");
}

static void test_is_string_empty(void) {
    test_section("Testing is_string_empty");

    TEST_ASSERT(is_string_empty("") == true, "Empty string");
    TEST_ASSERT(is_string_empty("   ") == true, "Whitespace only string");
    TEST_ASSERT(is_string_empty("\t\n ") == true, "Mixed whitespace string");
    TEST_ASSERT(is_string_empty("hello") == false, "Non-empty string");
    TEST_ASSERT(is_string_empty(" hello ") == false, "String with content and whitespace");
    TEST_ASSERT(is_string_empty(NULL) == true, "NULL string");
}

static void test_find_nth_char(void) {
    test_section("Testing find_nth_char");

    const char *test_str = "a:b:c:d";
    const char *result;

    result = find_nth_char(test_str, ':', 1);
    TEST_ASSERT(result == &test_str[1], "First colon found");

    result = find_nth_char(test_str, ':', 2);
    TEST_ASSERT(result == &test_str[3], "Second colon found");

    result = find_nth_char(test_str, ':', 3);
    TEST_ASSERT(result == &test_str[5], "Third colon found");

    result = find_nth_char(test_str, ':', 4);
    TEST_ASSERT_NULL(result, "Fourth colon not found");

    result = find_nth_char(test_str, 'x', 1);
    TEST_ASSERT_NULL(result, "Non-existent character");

    result = find_nth_char(NULL, ':', 1);
    TEST_ASSERT_NULL(result, "NULL string");

    result = find_nth_char(test_str, ':', 0);
    TEST_ASSERT_NULL(result, "Zero index");
}

int main(void) {
    test_init("String Utilities");

    test_safe_string_copy();
    test_trim_functions();
    test_is_string_empty();
    test_find_nth_char();

    return test_finish();
}