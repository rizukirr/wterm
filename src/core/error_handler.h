#pragma once

/**
 * @file error_handler.h
 * @brief WiFi connection error detection and handling
 */

#include "../../include/wterm/common.h"
#include <stdbool.h>

/**
 * @brief Connection error types
 */
typedef enum {
    CONN_ERROR_NONE = 0,
    CONN_ERROR_AUTH_FAILED,
    CONN_ERROR_NETWORK_UNAVAILABLE,
    CONN_ERROR_TIMEOUT,
    CONN_ERROR_WIFI_DISABLED,
    CONN_ERROR_PERMISSION_DENIED,
    CONN_ERROR_DHCP_TIMEOUT,
    CONN_ERROR_DNS_FAILURE,
    CONN_ERROR_CAPTIVE_PORTAL,
    CONN_ERROR_MAC_FILTERING,
    CONN_ERROR_UNSUPPORTED_SECURITY,
    CONN_ERROR_NETWORKMANAGER_NOT_RUNNING,
    CONN_ERROR_UNKNOWN
} connection_error_t;

/**
 * @brief Error information structure
 */
typedef struct {
    connection_error_t type;
    char message[256];
    char suggestion[512];
    bool can_retry;
    bool auto_fixable;
} error_info_t;

/**
 * @brief Parse nmcli error output to determine error type
 * @param error_output Error message from nmcli command
 * @return connection_error_t Error type identified
 */
connection_error_t parse_nmcli_error(const char* error_output);

/**
 * @brief Get detailed error information
 * @param error Error type
 * @param network_name Name of the network that failed
 * @return error_info_t Detailed error information
 */
error_info_t get_error_info(connection_error_t error, const char* network_name);

/**
 * @brief Check if WiFi adapter is enabled
 * @return bool true if WiFi is enabled, false otherwise
 */
bool is_wifi_enabled(void);

/**
 * @brief Check if NetworkManager service is running
 * @return bool true if NetworkManager is running, false otherwise
 */
bool is_networkmanager_running(void);

/**
 * @brief Test internet connectivity
 * @return bool true if internet is accessible, false otherwise
 */
bool test_internet_connectivity(void);

/**
 * @brief Attempt to enable WiFi adapter
 * @return bool true if successful, false otherwise
 */
bool auto_enable_wifi(void);

/**
 * @brief Get user-friendly error message
 * @param error Error type
 * @return const char* Human-readable error message
 */
const char* get_error_message(connection_error_t error);

/**
 * @brief Get suggested recovery actions
 * @param error Error type
 * @return const char* Suggested actions for user
 */
const char* get_error_suggestion(connection_error_t error);

/**
 * @brief Check if error can be automatically fixed
 * @param error Error type
 * @return bool true if auto-fix is available
 */
bool can_auto_fix_error(connection_error_t error);