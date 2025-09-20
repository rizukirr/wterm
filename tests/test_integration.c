/**
 * @file test_integration.c
 * @brief Integration tests for the complete system
 */

#include "test_utils.h"
#include "../src/core/network_scanner.h"
#include "../include/wterm/common.h"
#include <string.h>

static void test_original_bug_scenario(void) {
    test_section("Testing Original Bug Scenario - POCO F4 Network");

    // This test specifically verifies that the original bug is fixed
    // The bug was that networks with empty security fields (open networks)
    // were not being displayed

    network_info_t network;
    wterm_result_t result;

    // Test the exact line that was causing the original issue
    result = parse_network_line("POCO F4::89", &network);

    TEST_ASSERT_EQUAL_INT(WTERM_SUCCESS, result, "POCO F4 network should parse successfully");
    TEST_ASSERT_EQUAL_STR("POCO F4", network.ssid, "POCO F4 SSID should be preserved");
    TEST_ASSERT_EQUAL_STR("Open", network.security, "Empty security should become 'Open'");
    TEST_ASSERT_EQUAL_STR("89", network.signal, "Signal strength should be preserved");

    printf("✓ Original bug fix verified: POCO F4 network now displays correctly\n");
}

static void test_multiple_network_types(void) {
    test_section("Testing Multiple Network Types");

    // Simulate the exact output that nmcli was producing in the original issue
    const char *test_networks[] = {
        "POCO F4::89",                    // Open network (original problem)
        "Marcel-2.4G:WPA1 WPA2:27",      // Secured network
        "Fahmi  4G:WPA1 WPA2:25",        // Secured network with spaces
        "Azriel:WPA1 WPA2:20"            // Secured network
    };

    network_info_t networks[4];
    int successful_parses = 0;

    for (int i = 0; i < 4; i++) {
        wterm_result_t result = parse_network_line(test_networks[i], &networks[i]);
        if (result == WTERM_SUCCESS) {
            successful_parses++;
        }
    }

    TEST_ASSERT_EQUAL_INT(4, successful_parses, "All network types should parse successfully");

    // Verify specific networks
    TEST_ASSERT_EQUAL_STR("POCO F4", networks[0].ssid, "First network SSID");
    TEST_ASSERT_EQUAL_STR("Open", networks[0].security, "First network security (open)");

    TEST_ASSERT_EQUAL_STR("Marcel-2.4G", networks[1].ssid, "Second network SSID");
    TEST_ASSERT_EQUAL_STR("WPA1 WPA2", networks[1].security, "Second network security");

    printf("✓ All network types from original output now parse correctly\n");
}

static void test_network_list_with_original_data(void) {
    test_section("Testing Network List with Original Data");

    // This simulates what would happen if we had the original nmcli output
    network_list_t network_list;
    network_list.count = 0;

    const char *original_nmcli_output[] = {
        "POCO F4::89",
        "Marcel-2.4G:WPA1 WPA2:27",
        "Fahmi  4G:WPA1 WPA2:25",
        "Azriel:WPA1 WPA2:20"
    };

    // Manually parse each line (simulating what scan_wifi_networks does)
    for (int i = 0; i < 4 && network_list.count < MAX_NETWORKS; i++) {
        network_info_t network;
        wterm_result_t result = parse_network_line(original_nmcli_output[i], &network);

        if (result == WTERM_SUCCESS) {
            network_list.networks[network_list.count] = network;
            network_list.count++;
        }
    }

    TEST_ASSERT_EQUAL_INT(4, network_list.count, "All networks should be in the list");

    // Verify that POCO F4 is now included (it was missing in the original bug)
    bool poco_f4_found = false;
    for (int i = 0; i < network_list.count; i++) {
        if (strcmp(network_list.networks[i].ssid, "POCO F4") == 0) {
            poco_f4_found = true;
            TEST_ASSERT_EQUAL_STR("Open", network_list.networks[i].security, "POCO F4 should be marked as Open");
            break;
        }
    }

    TEST_ASSERT(poco_f4_found, "POCO F4 network should be found in the list");
    printf("✓ POCO F4 network is now included in network list (bug fixed!)\n");
}

static void test_memory_safety_validation(void) {
    test_section("Testing Memory Safety");

    network_info_t network;
    wterm_result_t result;

    // Test buffer bounds
    char very_long_line[1000];
    for (int i = 0; i < 500; i++) {
        very_long_line[i] = 'A';
    }
    very_long_line[500] = ':';
    for (int i = 501; i < 600; i++) {
        very_long_line[i] = 'B';
    }
    very_long_line[600] = ':';
    for (int i = 601; i < 999; i++) {
        very_long_line[i] = 'C';
    }
    very_long_line[999] = '\0';

    result = parse_network_line(very_long_line, &network);
    TEST_ASSERT_EQUAL_INT(WTERM_SUCCESS, result, "Very long line should not crash");

    // Verify bounds were respected
    TEST_ASSERT(strlen(network.ssid) < MAX_STR_SSID, "SSID bounds respected");
    TEST_ASSERT(strlen(network.security) < MAX_STR_SECURITY, "Security bounds respected");
    TEST_ASSERT(strlen(network.signal) < MAX_STR_SIGNAL, "Signal bounds respected");

    printf("✓ Memory safety validated - no buffer overflows\n");
}

int main(void) {
    test_init("Integration Tests");

    test_original_bug_scenario();
    test_multiple_network_types();
    test_network_list_with_original_data();
    test_memory_safety_validation();

    return test_finish();
}