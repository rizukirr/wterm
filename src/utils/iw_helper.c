/**
 * @file iw_helper.c
 * @brief Helper functions for iw command operations
 */

#define _POSIX_C_SOURCE 200809L
#include "iw_helper.h"
#include "string_utils.h"
#include "safe_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

bool iw_is_available(void) {
    return safe_command_exists("iw");
}

wterm_result_t iw_get_phy_index(const char *interface, int *phy_index) {
    if (!interface || !phy_index) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    if (!iw_is_available()) {
        return WTERM_ERROR_GENERAL;
    }

    // Execute: iw dev <interface> info | grep wiphy | awk '{print $2}'
    char command[256];
    snprintf(command, sizeof(command), "iw dev %s info 2>/dev/null | grep wiphy", interface);

    FILE *fp = popen(command, "r");
    if (!fp) {
        return WTERM_ERROR_NETWORK;
    }

    char buffer[128];
    bool found = false;

    if (fgets(buffer, sizeof(buffer), fp)) {
        // Parse line like: "    wiphy 0"
        char *wiphy_start = strstr(buffer, "wiphy ");
        if (wiphy_start) {
            wiphy_start += strlen("wiphy ");
            *phy_index = atoi(wiphy_start);
            found = true;
        }
    }

    pclose(fp);
    return found ? WTERM_SUCCESS : WTERM_ERROR_GENERAL;
}

wterm_result_t iw_check_ap_mode_support(const char *interface, bool *supports_ap) {
    if (!interface || !supports_ap) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    *supports_ap = false;

    if (!iw_is_available()) {
        return WTERM_ERROR_GENERAL;
    }

    // Get PHY index first
    int phy_index;
    wterm_result_t result = iw_get_phy_index(interface, &phy_index);
    if (result != WTERM_SUCCESS) {
        return result;
    }

    // Execute: iw phy phy<N> info | grep -A 10 "Supported interface modes"
    char command[256];
    snprintf(command, sizeof(command),
             "iw phy phy%d info 2>/dev/null | grep -A 10 'Supported interface modes'",
             phy_index);

    FILE *fp = popen(command, "r");
    if (!fp) {
        return WTERM_ERROR_NETWORK;
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), fp)) {
        // Look for "* AP" in the output
        if (strstr(buffer, "* AP")) {
            *supports_ap = true;
            break;
        }
    }

    pclose(fp);
    return WTERM_SUCCESS;
}

wterm_result_t iw_check_5ghz_support(const char *interface, bool *supports_5ghz) {
    if (!interface || !supports_5ghz) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    *supports_5ghz = false;

    if (!iw_is_available()) {
        return WTERM_ERROR_GENERAL;
    }

    // Get PHY index first
    int phy_index;
    wterm_result_t result = iw_get_phy_index(interface, &phy_index);
    if (result != WTERM_SUCCESS) {
        return result;
    }

    // Execute: iw phy phy<N> info | grep "Band 2:"
    // Band 1 is 2.4GHz, Band 2 is 5GHz
    char command[256];
    snprintf(command, sizeof(command),
             "iw phy phy%d info 2>/dev/null | grep 'Band 2:'",
             phy_index);

    FILE *fp = popen(command, "r");
    if (!fp) {
        return WTERM_ERROR_NETWORK;
    }

    char buffer[256];
    if (fgets(buffer, sizeof(buffer), fp)) {
        // If we find "Band 2:" line, 5GHz is supported
        if (strstr(buffer, "Band 2:")) {
            *supports_5ghz = true;
        }
    }

    pclose(fp);
    return WTERM_SUCCESS;
}

wterm_result_t iw_check_concurrent_mode_support(const char *interface, bool *supports_concurrent) {
    if (!interface || !supports_concurrent) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    *supports_concurrent = false;

    if (!iw_is_available()) {
        return WTERM_ERROR_GENERAL;
    }

    // Get PHY index first
    int phy_index;
    wterm_result_t result = iw_get_phy_index(interface, &phy_index);
    if (result != WTERM_SUCCESS) {
        return result;
    }

    // Execute: iw phy phy<N> info
    char command[256];
    snprintf(command, sizeof(command), "iw phy phy%d info 2>/dev/null", phy_index);

    FILE *fp = popen(command, "r");
    if (!fp) {
        return WTERM_ERROR_NETWORK;
    }

    char buffer[512];
    bool in_combinations = false;
    bool found_managed = false;
    bool found_ap = false;
    int max_channels = 0;

    // Parse output looking for "valid interface combinations"
    while (fgets(buffer, sizeof(buffer), fp)) {
        // Look for the start of interface combinations section
        if (strstr(buffer, "valid interface combinations")) {
            in_combinations = true;
            continue;
        }

        if (in_combinations) {
            // End of combinations section (next section starts without indentation)
            if (buffer[0] != ' ' && buffer[0] != '\t') {
                break;
            }

            // Look for: #{ managed } <= 1
            if (strstr(buffer, "#{ managed }")) {
                found_managed = true;
            }

            // Look for: #{ AP } <= 1
            if (strstr(buffer, "#{ AP }")) {
                found_ap = true;
            }

            // Look for: #channels <= N
            if (strstr(buffer, "#channels")) {
                char *channels_pos = strstr(buffer, "<= ");
                if (channels_pos) {
                    channels_pos += 3; // Skip "<= "
                    max_channels = atoi(channels_pos);
                }
            }

            // Check if we have a complete combination entry
            if (strstr(buffer, "total <=")) {
                // If this combination has both managed and AP with max_channels >= 1,
                // concurrent mode is supported
                if (found_managed && found_ap && max_channels >= 1) {
                    *supports_concurrent = true;
                    break;
                }
                // Reset for next combination
                found_managed = false;
                found_ap = false;
                max_channels = 0;
            }
        }
    }

    pclose(fp);
    return WTERM_SUCCESS;
}

wterm_result_t iw_check_interface_connected(const char *interface, bool *is_connected) {
    if (!interface || !is_connected) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    *is_connected = false;

    if (!iw_is_available()) {
        return WTERM_ERROR_GENERAL;
    }

    // Execute: iw dev <interface> link
    char command[256];
    snprintf(command, sizeof(command), "iw dev %s link 2>/dev/null", interface);

    FILE *fp = popen(command, "r");
    if (!fp) {
        return WTERM_ERROR_NETWORK;
    }

    char buffer[256];
    if (fgets(buffer, sizeof(buffer), fp)) {
        // If connected, output starts with "Connected to"
        if (strstr(buffer, "Connected to")) {
            *is_connected = true;
        }
    }

    pclose(fp);
    return WTERM_SUCCESS;
}

wterm_result_t iw_get_connected_channel(const char *interface, int *channel) {
    if (!interface || !channel) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    *channel = -1; // Default: not connected

    if (!iw_is_available()) {
        return WTERM_ERROR_GENERAL;
    }

    // Execute: iw dev <interface> link
    char command[256];
    snprintf(command, sizeof(command), "iw dev %s link 2>/dev/null", interface);

    FILE *fp = popen(command, "r");
    if (!fp) {
        return WTERM_ERROR_NETWORK;
    }

    char buffer[256];
    int frequency = 0;

    while (fgets(buffer, sizeof(buffer), fp)) {
        // Look for line like: "freq: 2437"
        if (strstr(buffer, "freq:")) {
            char *freq_pos = strstr(buffer, "freq:");
            if (freq_pos) {
                freq_pos += 5; // Skip "freq:"
                frequency = atoi(freq_pos);
                break;
            }
        }
    }

    pclose(fp);

    if (frequency == 0) {
        // Not connected or no frequency information
        return WTERM_SUCCESS;
    }

    // Convert frequency to channel number
    if (frequency >= 2412 && frequency <= 2484) {
        // 2.4GHz band
        if (frequency == 2484) {
            *channel = 14; // Special case for channel 14
        } else {
            *channel = (frequency - 2407) / 5;
        }
    } else if (frequency >= 5160 && frequency <= 5885) {
        // 5GHz band
        *channel = (frequency - 5000) / 5;
    } else if (frequency >= 5945 && frequency <= 7125) {
        // 6GHz band (802.11ax)
        *channel = (frequency - 5950) / 5;
    } else {
        // Unknown frequency band
        return WTERM_ERROR_GENERAL;
    }

    return WTERM_SUCCESS;
}
