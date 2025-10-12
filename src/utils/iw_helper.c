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
