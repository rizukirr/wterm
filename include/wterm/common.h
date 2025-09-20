#pragma once

/**
 * @file common.h
 * @brief Common definitions and constants for wterm project
 */

#include <stddef.h>
#include <stdbool.h>

// Version information
#define WTERM_VERSION_MAJOR 2
#define WTERM_VERSION_MINOR 0
#define WTERM_VERSION_PATCH 0
#define WTERM_VERSION_STRING "2.0.0"

// Network configuration limits
#define MAX_STR_SSID 33         // 32 bytes SSID + '\0'
#define MAX_STR_SECURITY 17     // long enough for WPA2-Enterprise
#define MAX_STR_SIGNAL 33       // flexible signal representation
#define MAX_NETWORKS 32         // handle up to 32 available networks

// Command definitions - moved to backend implementations
// Legacy defines kept for backward compatibility with get_networks.c
#define CMD_WIFI_LIST "nmcli -t -f SSID,SECURITY,SIGNAL device wifi list"

// Return codes
typedef enum {
    WTERM_SUCCESS = 0,
    WTERM_ERROR_GENERAL = 1,
    WTERM_ERROR_NETWORK = 2,
    WTERM_ERROR_PARSE = 3,
    WTERM_ERROR_MEMORY = 4,
    WTERM_ERROR_INVALID_INPUT = 5
} wterm_result_t;

// Network information structure
typedef struct {
    char ssid[MAX_STR_SSID];
    char security[MAX_STR_SECURITY];
    char signal[MAX_STR_SIGNAL];
} network_info_t;

// Network list structure
typedef struct {
    network_info_t networks[MAX_NETWORKS];
    int count;
} network_list_t;