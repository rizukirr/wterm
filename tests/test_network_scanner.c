/**
 * @file test_network_scanner.c
 * @brief Unit tests for network scanner functionality
 */

#include "test_utils.h"
#include "../src/core/network_scanner.h"
#include "../include/wterm/common.h"
#include <string.h>

static void test_parse_network_line(void) {
    test_section("Testing parse_network_line");

    network_info_t network;
    wterm_result_t result;

    // Test normal WiFi network with security
    result = parse_network_line("MyNetwork:WPA2:75", &network);
    TEST_ASSERT_EQUAL_INT(WTERM_SUCCESS, result, "Normal network parsing");
    TEST_ASSERT_EQUAL_STR("MyNetwork", network.ssid, "SSID parsing");
    TEST_ASSERT_EQUAL_STR("WPA2", network.security, "Security parsing");
    TEST_ASSERT_EQUAL_STR("75", network.signal, "Signal parsing");

    // Test open network (empty security field) - this was the original bug!
    result = parse_network_line("POCO F4::89", &network);
    TEST_ASSERT_EQUAL_INT(WTERM_SUCCESS, result, "Open network parsing");
    TEST_ASSERT_EQUAL_STR("POCO F4", network.ssid, "Open network SSID");
    TEST_ASSERT_EQUAL_STR("Open", network.security, "Open network security");
    TEST_ASSERT_EQUAL_STR("89", network.signal, "Open network signal");

    // Test network with spaces in SSID
    result = parse_network_line("My Home WiFi:WPA1 WPA2:62", &network);
    TEST_ASSERT_EQUAL_INT(WTERM_SUCCESS, result, "SSID with spaces parsing");
    TEST_ASSERT_EQUAL_STR("My Home WiFi", network.ssid, "SSID with spaces");
    TEST_ASSERT_EQUAL_STR("WPA1 WPA2", network.security, "Multiple security types");

    // Test invalid formats
    result = parse_network_line("InvalidFormat", &network);
    TEST_ASSERT_EQUAL_INT(WTERM_ERROR_PARSE, result, "Invalid format - no colons");

    result = parse_network_line("OnlyOne:", &network);
    TEST_ASSERT_EQUAL_INT(WTERM_ERROR_PARSE, result, "Invalid format - only one colon");

    result = parse_network_line(NULL, &network);
    TEST_ASSERT_EQUAL_INT(WTERM_ERROR_INVALID_INPUT, result, "NULL input");

    result = parse_network_line("Test:WPA:75", NULL);
    TEST_ASSERT_EQUAL_INT(WTERM_ERROR_INVALID_INPUT, result, "NULL network struct");
}

static void test_network_parsing_edge_cases(void) {
    test_section("Testing network parsing edge cases");

    network_info_t network;
    wterm_result_t result;

    // Test very long SSID (should be truncated)
    char long_ssid[100];
    for (int i = 0; i < 99; i++) {
        long_ssid[i] = 'A';
    }
    long_ssid[99] = '\0';

    char test_line[150];
    snprintf(test_line, sizeof(test_line), "%s:WPA2:50", long_ssid);

    result = parse_network_line(test_line, &network);
    TEST_ASSERT_EQUAL_INT(WTERM_SUCCESS, result, "Long SSID parsing");
    TEST_ASSERT(strlen(network.ssid) < MAX_STR_SSID, "SSID truncation");

    // Test empty SSID
    result = parse_network_line(":WPA2:50", &network);
    TEST_ASSERT_EQUAL_INT(WTERM_SUCCESS, result, "Empty SSID parsing");
    TEST_ASSERT_EQUAL_STR("", network.ssid, "Empty SSID content");

    // Test empty signal
    result = parse_network_line("TestSSID:WPA2:", &network);
    TEST_ASSERT_EQUAL_INT(WTERM_SUCCESS, result, "Empty signal parsing");
    TEST_ASSERT_EQUAL_STR("", network.signal, "Empty signal content");

    // Test whitespace trimming
    result = parse_network_line("  SpacedSSID  : WPA2 :  75  ", &network);
    TEST_ASSERT_EQUAL_INT(WTERM_SUCCESS, result, "Whitespace parsing");
    // Note: The current implementation doesn't trim around colons, only trailing
}

static void test_network_list_initialization(void) {
    test_section("Testing network list operations");

    network_list_t network_list;

    // Initialize and verify clean state
    network_list.count = -1; // Set to invalid value
    memset(network_list.networks, 0xFF, sizeof(network_list.networks)); // Fill with junk

    // The scan function should initialize properly
    // Note: This test might fail if nmcli is not available, which is expected
    wterm_result_t result = scan_wifi_networks(&network_list);
    (void)result; // Suppress unused variable warning

    // We don't test the specific result since nmcli might not be available in test environment
    // But we test that the structure is properly initialized
    TEST_ASSERT(network_list.count >= 0, "Network count should be non-negative");
    TEST_ASSERT(network_list.count <= MAX_NETWORKS, "Network count within limits");

    if (network_list.count > 0) {
        // Test that first network has been properly initialized
        TEST_ASSERT(strlen(network_list.networks[0].ssid) < MAX_STR_SSID, "First network SSID length");
        TEST_ASSERT(strlen(network_list.networks[0].security) < MAX_STR_SECURITY, "First network security length");
        TEST_ASSERT(strlen(network_list.networks[0].signal) < MAX_STR_SIGNAL, "First network signal length");
    }
}

int main(void) {
    test_init("Network Scanner");

    test_parse_network_line();
    test_network_parsing_edge_cases();
    test_network_list_initialization();

    return test_finish();
}