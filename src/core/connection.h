#pragma once

/**
 * @file connection.h
 * @brief WiFi connection management with password handling
 */

#include "../../include/wterm/common.h"
#include "error_handler.h"
#include <stdbool.h>

/**
 * @brief Connection result structure
 */
typedef struct {
    wterm_result_t result;
    connection_error_t error_type;
    char error_message[256];
    bool connected;
} connection_result_t;

/**
 * @brief Connection attempt information
 */
typedef struct {
    char ssid[MAX_STR_SSID];
    char security[MAX_STR_SECURITY];
    char password[256];  // Temporary storage during connection
    int attempt_count;
    bool is_open_network;
} connection_attempt_t;

/**
 * @brief Current connection status
 */
typedef struct {
    bool is_connected;
    char connected_ssid[MAX_STR_SSID];
    char connection_name[MAX_STR_SSID];  // Connection profile name (may differ from SSID)
    char connection_uuid[64];
    char ip_address[16];
} connection_status_t;

/**
 * @brief Connect to an open WiFi network
 * @param ssid Network SSID to connect to
 * @return connection_result_t Connection result and error info
 */
connection_result_t connect_to_open_network(const char* ssid);

/**
 * @brief Connect to a secured WiFi network with password
 * @param ssid Network SSID to connect to
 * @param password Network password
 * @return connection_result_t Connection result and error info
 */
connection_result_t connect_to_secured_network(const char* ssid, const char* password);

/**
 * @brief Get current WiFi connection status
 * @return connection_status_t Current connection information
 */
connection_status_t get_connection_status(void);

/**
 * @brief Disconnect from current WiFi network
 * @return wterm_result_t Result of disconnect operation
 */
wterm_result_t disconnect_current_network(void);

/**
 * @brief Check if a network requires a password
 * @param security Security type string from nmcli
 * @return bool true if password is required, false for open networks
 */
bool network_requires_password(const char* security);

/**
 * @brief Validate password requirements for security type
 * @param password Password to validate
 * @param security Security type (WPA, WPA2, etc.)
 * @return bool true if password meets requirements
 */
bool validate_password(const char* password, const char* security);

/**
 * @brief Clear password from memory securely
 * @param password Password buffer to clear
 * @param length Length of password buffer
 */
void secure_clear_password(char* password, size_t length);

/**
 * @brief Get signal strength as visual bars
 * @param signal_str Signal strength string from nmcli
 * @return const char* Visual representation of signal strength
 */
const char* get_signal_bars(const char* signal_str);

/**
 * @brief Check if currently connected to a specific network
 * @param ssid Network SSID to check
 * @return bool true if connected to this network
 */
bool is_connected_to_network(const char* ssid);

/**
 * @brief Monitor connection progress with timeout
 * @param ssid Network being connected to
 * @param timeout_seconds Maximum time to wait
 * @return connection_result_t Final connection result
 */
connection_result_t monitor_connection_progress(const char* ssid, int timeout_seconds);

/**
 * @brief Check if a saved connection exists for the given SSID
 * @param ssid Network SSID to check
 * @return bool true if a saved connection exists, false otherwise
 */
bool is_saved_connection(const char* ssid);

/**
 * @brief Initialize connection cancellation state
 * Resets the cancellation flag to allow new connections
 */
void init_connection_cancel(void);

/**
 * @brief Request cancellation of ongoing connection attempt
 * Sets a flag that will be checked during connection polling
 */
void request_connection_cancel(void);

/**
 * @brief Check if connection cancellation was requested
 * @return bool true if cancellation was requested, false otherwise
 */
bool is_connection_cancelled(void);