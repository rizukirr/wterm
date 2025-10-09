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

bool iw_get_first_wifi_interface(char *interface, size_t size) {
    if (!interface || size == 0) {
        return false;
    }

    if (!iw_is_available()) {
        return false;
    }

    // Execute: iw dev | grep Interface | awk '{print $2}' | head -1
    FILE *fp = popen("iw dev 2>/dev/null | grep Interface", "r");
    if (!fp) {
        return false;
    }

    char buffer[256];
    bool found = false;

    if (fgets(buffer, sizeof(buffer), fp)) {
        // Parse line like: "Interface wlan0"
        char *iface_start = strstr(buffer, "Interface ");
        if (iface_start) {
            iface_start += strlen("Interface ");
            // Extract interface name (stop at whitespace)
            char *space = strchr(iface_start, ' ');
            char *newline = strchr(iface_start, '\n');

            size_t len = strlen(iface_start);
            if (space && (size_t)(space - iface_start) < len) {
                len = space - iface_start;
            }
            if (newline && (size_t)(newline - iface_start) < len) {
                len = newline - iface_start;
            }

            if (len > 0 && len < size) {
                strncpy(interface, iface_start, len);
                interface[len] = '\0';
                found = true;
            }
        }
    }

    pclose(fp);
    return found;
}

wterm_result_t iw_get_all_wifi_interfaces(char interfaces[][MAX_STR_INTERFACE],
                                          int max_count, int *count) {
    if (!interfaces || !count || max_count <= 0) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    *count = 0;

    if (!iw_is_available()) {
        return WTERM_ERROR_GENERAL;
    }

    FILE *fp = popen("iw dev 2>/dev/null", "r");
    if (!fp) {
        return WTERM_ERROR_NETWORK;
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), fp) && *count < max_count) {
        // Look for lines like: "    Interface wlan0"
        char *iface_start = strstr(buffer, "Interface ");
        if (iface_start) {
            iface_start += strlen("Interface ");

            // Extract interface name
            char *space = strchr(iface_start, ' ');
            char *newline = strchr(iface_start, '\n');

            size_t len = strlen(iface_start);
            if (space && (size_t)(space - iface_start) < len) {
                len = space - iface_start;
            }
            if (newline && (size_t)(newline - iface_start) < len) {
                len = newline - iface_start;
            }

            if (len > 0 && len < MAX_STR_INTERFACE) {
                strncpy(interfaces[*count], iface_start, len);
                interfaces[*count][len] = '\0';
                (*count)++;
            }
        }
    }

    pclose(fp);
    return (*count > 0) ? WTERM_SUCCESS : WTERM_ERROR_GENERAL;
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

wterm_result_t iw_check_association(const char *interface, bool *is_associated) {
    if (!interface || !is_associated) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    *is_associated = false;

    if (!iw_is_available()) {
        return WTERM_ERROR_GENERAL;
    }

    char command[256];
    snprintf(command, sizeof(command), "iw dev %s link 2>&1", interface);

    FILE *fp = popen(command, "r");
    if (!fp) {
        return WTERM_ERROR_NETWORK;
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), fp)) {
        // Look for "Connected to" which indicates association
        if (strstr(buffer, "Connected to")) {
            *is_associated = true;
            break;
        }
    }

    pclose(fp);
    return WTERM_SUCCESS;
}

wterm_result_t iw_get_link_quality(const char *interface, int *signal_dbm,
                                   int *tx_bitrate, int *rx_bitrate) {
    if (!interface || !signal_dbm || !tx_bitrate || !rx_bitrate) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    *signal_dbm = 0;
    *tx_bitrate = 0;
    *rx_bitrate = 0;

    if (!iw_is_available()) {
        return WTERM_ERROR_GENERAL;
    }

    char command[256];
    snprintf(command, sizeof(command), "iw dev %s link 2>&1", interface);

    FILE *fp = popen(command, "r");
    if (!fp) {
        return WTERM_ERROR_NETWORK;
    }

    char buffer[256];
    bool found_connection = false;

    while (fgets(buffer, sizeof(buffer), fp)) {
        // Check if connected first
        if (strstr(buffer, "Connected to")) {
            found_connection = true;
        }

        // Parse signal: "signal: -45 dBm"
        if (strstr(buffer, "signal:")) {
            char *signal_str = strstr(buffer, "signal:");
            if (signal_str) {
                signal_str += strlen("signal:");
                *signal_dbm = atoi(signal_str);
            }
        }

        // Parse TX bitrate: "tx bitrate: 72.2 MBit/s"
        if (strstr(buffer, "tx bitrate:")) {
            char *bitrate_str = strstr(buffer, "tx bitrate:");
            if (bitrate_str) {
                bitrate_str += strlen("tx bitrate:");
                while (*bitrate_str == ' ') bitrate_str++;
                *tx_bitrate = (int)atof(bitrate_str);
            }
        }

        // Parse RX bitrate: "rx bitrate: 72.2 MBit/s"
        if (strstr(buffer, "rx bitrate:")) {
            char *bitrate_str = strstr(buffer, "rx bitrate:");
            if (bitrate_str) {
                bitrate_str += strlen("rx bitrate:");
                while (*bitrate_str == ' ') bitrate_str++;
                *rx_bitrate = (int)atof(bitrate_str);
            }
        }
    }

    pclose(fp);
    return found_connection ? WTERM_SUCCESS : WTERM_ERROR_GENERAL;
}
