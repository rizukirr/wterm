/**
 * @file test_security.c
 * @brief Security-focused test suite for input sanitization
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "../src/utils/input_sanitizer.h"
#include "test_utils.h"

// Test shell escaping functionality
static void test_shell_escape(void) {
    printf("Testing shell_escape()...\n");
    char output[256];

    // Test normal strings
    TEST_ASSERT(shell_escape("hello", output, sizeof(output)), "Basic string escaping failed");
    TEST_ASSERT_EQUAL_STR(output, "'hello'", "Basic escape incorrect");

    // Test strings with single quotes
    TEST_ASSERT(shell_escape("it's", output, sizeof(output)), "Single quote escaping failed");
    TEST_ASSERT_EQUAL_STR(output, "'it'\\''s'", "Single quote escape incorrect");

    // Test strings with command injection attempts
    TEST_ASSERT(shell_escape("$(whoami)", output, sizeof(output)), "Command substitution escape failed");
    TEST_ASSERT_EQUAL_STR(output, "'$(whoami)'", "Command substitution not escaped");

    TEST_ASSERT(shell_escape("`id`", output, sizeof(output)), "Backtick escape failed");
    TEST_ASSERT_EQUAL_STR(output, "'`id`'", "Backtick not escaped");

    // Test buffer overflow protection
    char small_buf[10];
    TEST_ASSERT(!shell_escape("very long string that won't fit", small_buf, sizeof(small_buf)),
                "Buffer overflow not detected");

    // Test NULL handling
    TEST_ASSERT(!shell_escape(NULL, output, sizeof(output)), "NULL input not handled");
    TEST_ASSERT(!shell_escape("test", NULL, 256), "NULL output not handled");

    printf("  All shell_escape tests passed\n\n");
}

// Test SSID validation
static void test_validate_ssid(void) {
    printf("Testing validate_ssid()...\n");

    // Valid SSIDs
    TEST_ASSERT(validate_ssid("MyWiFi"), "Valid SSID rejected");
    TEST_ASSERT(validate_ssid("Test-Network_123"), "Valid SSID with special chars rejected");
    TEST_ASSERT(validate_ssid("a"), "Single char SSID rejected");

    // 32-byte SSID (maximum)
    char max_ssid[33];
    memset(max_ssid, 'a', 32);
    max_ssid[32] = '\0';
    TEST_ASSERT(validate_ssid(max_ssid), "32-byte SSID rejected");

    // Invalid SSIDs
    TEST_ASSERT(!validate_ssid(""), "Empty SSID accepted");
    TEST_ASSERT(!validate_ssid(NULL), "NULL SSID accepted");

    // 33-byte SSID (too long)
    char long_ssid[34];
    memset(long_ssid, 'a', 33);
    long_ssid[33] = '\0';
    TEST_ASSERT(!validate_ssid(long_ssid), "Too long SSID accepted");

    printf("  All validate_ssid tests passed\n\n");
}

// Test interface name validation
static void test_validate_interface(void) {
    printf("Testing validate_interface_name()...\n");

    // Valid interface names
    TEST_ASSERT(validate_interface_name("wlan0"), "Valid interface rejected");
    TEST_ASSERT(validate_interface_name("eth0"), "Valid interface rejected");
    TEST_ASSERT(validate_interface_name("wlan_1"), "Interface with underscore rejected");

    // Invalid interface names
    TEST_ASSERT(!validate_interface_name(""), "Empty interface accepted");
    TEST_ASSERT(!validate_interface_name(NULL), "NULL interface accepted");
    TEST_ASSERT(!validate_interface_name("-invalid"), "Dash-prefixed interface accepted");
    TEST_ASSERT(!validate_interface_name("wlan0; rm -rf /"), "Command injection accepted");
    TEST_ASSERT(!validate_interface_name("wlan0`whoami`"), "Backtick injection accepted");

    // Too long (>15 chars)
    TEST_ASSERT(!validate_interface_name("verylonginterfacename"), "Too long interface accepted");

    printf("  All validate_interface_name tests passed\n\n");
}

// Test hotspot name validation
static void test_validate_hotspot_name(void) {
    printf("Testing validate_hotspot_name()...\n");

    // Valid names
    TEST_ASSERT(validate_hotspot_name("MyHotspot"), "Valid name rejected");
    TEST_ASSERT(validate_hotspot_name("Test_Hotspot-123"), "Valid name with specials rejected");
    TEST_ASSERT(validate_hotspot_name("a"), "Single char name rejected");

    // Invalid names
    TEST_ASSERT(!validate_hotspot_name(""), "Empty name accepted");
    TEST_ASSERT(!validate_hotspot_name(NULL), "NULL name accepted");
    TEST_ASSERT(!validate_hotspot_name("name;rm -rf /"), "Semicolon injection accepted");
    TEST_ASSERT(!validate_hotspot_name("name$(whoami)"), "Command substitution accepted");

    // Test 64-char limit
    char max_name[65];
    memset(max_name, 'a', 64);
    max_name[64] = '\0';
    TEST_ASSERT(validate_hotspot_name(max_name), "64-char name rejected");

    char long_name[66];
    memset(long_name, 'a', 65);
    long_name[65] = '\0';
    TEST_ASSERT(!validate_hotspot_name(long_name), "Too long name accepted");

    printf("  All validate_hotspot_name tests passed\n\n");
}

// Test format specifier detection
static void test_format_specifiers(void) {
    printf("Testing contains_format_specifiers()...\n");

    // Strings with format specifiers
    TEST_ASSERT(contains_format_specifiers("%s"), "Failed to detect %s");
    TEST_ASSERT(contains_format_specifiers("%d"), "Failed to detect %d");
    TEST_ASSERT(contains_format_specifiers("%x"), "Failed to detect %x");
    TEST_ASSERT(contains_format_specifiers("text %s text"), "Failed to detect embedded %s");
    TEST_ASSERT(contains_format_specifiers("%%"), "Failed to detect %%");

    // Strings without format specifiers
    TEST_ASSERT(!contains_format_specifiers("normal text"), "False positive on normal text");
    TEST_ASSERT(!contains_format_specifiers("100% complete"), "False positive on percent");
    TEST_ASSERT(!contains_format_specifiers(""), "False positive on empty string");
    TEST_ASSERT(!contains_format_specifiers(NULL), "False positive on NULL");

    printf("  All contains_format_specifiers tests passed\n\n");
}

// Test command injection prevention
static void test_injection_prevention(void) {
    printf("Testing command injection prevention...\n");

    const char *injection_attempts[] = {
        "; rm -rf /",
        "$(whoami)",
        "`id`",
        "test && echo pwned",
        "test | cat /etc/passwd",
        "test > /tmp/evil",
        "test & background",
        "test\nwhoami",
        "test\\x00whoami",
        NULL
    };

    char escaped[512];
    for (int i = 0; injection_attempts[i] != NULL; i++) {
        bool result = shell_escape(injection_attempts[i], escaped, sizeof(escaped));
        TEST_ASSERT(result, "Shell escape failed for injection attempt");

        // Verify the escaped string is wrapped in single quotes
        TEST_ASSERT(escaped[0] == '\'', "Escaped string doesn't start with single quote");

        // Verify no unescaped single quotes (except the wrapping ones)
        bool found_unescaped = false;
        for (size_t j = 1; j < strlen(escaped) - 1; j++) {
            if (escaped[j] == '\'' &&
                !(j > 0 && escaped[j-1] == '\\')) {
                // Check if it's part of the '\'' escape sequence
                if (!(j >= 2 && escaped[j-2] == '\'' && escaped[j-1] == '\\' &&
                      j+1 < strlen(escaped) && escaped[j+1] == '\'')) {
                    found_unescaped = true;
                    break;
                }
            }
        }
        TEST_ASSERT(!found_unescaped, "Found unescaped single quote in escaped string");
    }

    printf("  All injection prevention tests passed\n\n");
}

// Test string sanitization
static void test_sanitize_string(void) {
    printf("Testing sanitize_string()...\n");

    char output[256];

    // Test normal string
    TEST_ASSERT(sanitize_string("Hello World", output, sizeof(output)), "Sanitization failed");
    TEST_ASSERT_EQUAL_STR(output, "Hello World", "Normal string modified");

    // Test dangerous characters get replaced
    TEST_ASSERT(sanitize_string("hello;world", output, sizeof(output)), "Sanitization failed");
    TEST_ASSERT_EQUAL_STR(output, "hello_world", "Semicolon not sanitized");

    TEST_ASSERT(sanitize_string("test$(whoami)", output, sizeof(output)), "Sanitization failed");
    TEST_ASSERT_EQUAL_STR(output, "test__whoami_", "Command chars not sanitized");

    // Test allowed characters preserved
    TEST_ASSERT(sanitize_string("test-name_123.txt", output, sizeof(output)), "Sanitization failed");
    TEST_ASSERT_EQUAL_STR(output, "test-name_123.txt", "Valid chars modified");

    printf("  All sanitize_string tests passed\n\n");
}

int main(void) {
    test_init("Security Tests");

    printf("=== Security Test Suite ===\n\n");

    test_shell_escape();
    test_validate_ssid();
    test_validate_interface();
    test_validate_hotspot_name();
    test_format_specifiers();
    test_injection_prevention();
    test_sanitize_string();

    test_finish();
    return 0;
}
