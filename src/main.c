/**
 * @file main.c
 * @brief Main entry point for wterm WiFi management tool
 */

#include "wterm/common.h"
#include "core/network_scanner.h"
#include "core/connection.h"
#include "fzf_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>


static void print_usage(const char *program_name) {
    printf("Usage: %s [OPTION]\n\n", program_name);
    printf("Connect to WiFi networks with a simple interface.\n\n");
    printf("Options:\n");
    printf("  -h, --help     Show this help message\n\n");
    printf("Interface:\n");
    printf("  ↑↓             Navigate networks\n");
    printf("  Enter          Connect to selected network\n");
    printf("  🔄 Rescan      Refresh network list\n");
    printf("  Type           Search networks\n");
    printf("  q/Esc          Quit\n\n");
    printf("Examples:\n");
    printf("  %s          # Show network selection interface\n", program_name);
    printf("  %s --help   # Show this help\n", program_name);
}

static wterm_result_t handle_list_networks(void) {
    network_list_t network_list;
    wterm_result_t result = scan_wifi_networks(&network_list);

    if (result != WTERM_SUCCESS) {
        fprintf(stderr, "Failed to scan WiFi networks\n");
        return result;
    }

    display_networks(&network_list);
    return WTERM_SUCCESS;
}


static wterm_result_t scan_networks_with_loading(network_list_t* network_list, bool is_rescan) {
    const char* message = is_rescan ? "Rescanning networks..." : "Scanning networks...";

    show_loading_animation(message);

    wterm_result_t result;
    if (is_rescan) {
        // Use silent rescan to avoid verbose output
        result = rescan_wifi_networks_silent(true);
        if (result == WTERM_SUCCESS) {
            result = scan_wifi_networks(network_list);
        }
    } else {
        result = scan_wifi_networks(network_list);
    }

    hide_loading_animation();

    if (result != WTERM_SUCCESS) {
        fprintf(stderr, "Failed to scan WiFi networks\n");
    }

    return result;
}

static wterm_result_t handle_fzf_mode(void) {
    // Check if fzf is available
    if (!fzf_is_available()) {
        fprintf(stderr, "fzf not found - falling back to text mode\n");
        fprintf(stderr, "Install fzf for interactive selection: https://github.com/junegunn/fzf\n");
        return handle_list_networks();
    }

    while (true) {
        // Scan networks with loading indicator
        network_list_t network_list;
        wterm_result_t result = scan_networks_with_loading(&network_list, false);
        if (result != WTERM_SUCCESS) {
            return result;
        }

        // Show fzf selection
        char selected_ssid[MAX_STR_SSID];
        if (!fzf_select_network_proper(&network_list, selected_ssid, sizeof(selected_ssid))) {
            fzf_show_message("No network selected.");
            return WTERM_SUCCESS;
        }

        // Check if user selected rescan
        if (strcmp(selected_ssid, "RESCAN") == 0) {
            result = scan_networks_with_loading(&network_list, true);
            if (result != WTERM_SUCCESS) {
                return result;
            }
            continue; // Show selection again with new networks
        }

        // Find the selected network in our list
        network_info_t* selected_network = NULL;
        for (int i = 0; i < network_list.count; i++) {
            if (strcmp(network_list.networks[i].ssid, selected_ssid) == 0) {
                selected_network = &network_list.networks[i];
                break;
            }
        }

        if (!selected_network) {
            fprintf(stderr, "Selected network not found in scan results\n");
            return WTERM_ERROR_GENERAL;
        }

        // Handle connection
        connection_result_t conn_result;
        if (network_requires_password(selected_network->security)) {
            char password[256];
            if (!fzf_get_password(selected_ssid, password, sizeof(password))) {
                fzf_show_message("Connection cancelled.");
                return WTERM_SUCCESS;
            }

            fzf_show_message("Connecting...");
            conn_result = connect_to_secured_network(selected_ssid, password);

            // Clear password from memory
            memset(password, 0, sizeof(password));
        } else {
            fzf_show_message("Connecting to open network...");
            conn_result = connect_to_open_network(selected_ssid);
        }

        // Show result
        if (conn_result.result == WTERM_SUCCESS) {
            fzf_show_message("✓ Connected successfully!");
        } else {
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg), "✗ Connection failed: %s", conn_result.error_message);
            fzf_show_message(error_msg);
        }

        return conn_result.result;
    }
}

int main(int argc, char *argv[]) {
    // Only handle --help option
    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage(argv[0]);
            return WTERM_SUCCESS;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[1]);
            fprintf(stderr, "Use --help for usage information.\n");
            return WTERM_ERROR_INVALID_INPUT;
        }
    }

    // Default action: show fzf interface
    wterm_result_t result = handle_fzf_mode();
    return result;
}