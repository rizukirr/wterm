/**
 * @file connection.c
 * @brief WiFi connection management implementation
 */

#define _POSIX_C_SOURCE 200809L
#include "connection.h"
#include "error_handler.h"
#include "../utils/string_utils.h"
#include "../utils/input_sanitizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

// Helper function to execute nmcli connection command and parse result
static connection_result_t execute_nmcli_connect(const char* command, const char* ssid) {
    connection_result_t result = {0};

    FILE *fp = popen(command, "r");
    if (!fp) {
        result.result = WTERM_ERROR_NETWORK;
        result.error_type = CONN_ERROR_NETWORKMANAGER_NOT_RUNNING;
        safe_string_copy(result.error_message, "Failed to execute nmcli command", sizeof(result.error_message));
        return result;
    }

    char error_output[512] = {0};
    char buffer[256];
    size_t current_len = 0;
    while (fgets(buffer, sizeof(buffer), fp)) {
        size_t remaining = sizeof(error_output) - current_len - 1;
        if (remaining > 0) {
            strncat(error_output, buffer, remaining);
            current_len = strlen(error_output);
        }
    }

    int exit_code = pclose(fp);

    if (exit_code == 0) {
        result.result = WTERM_SUCCESS;
        result.connected = true;
        snprintf(result.error_message, sizeof(result.error_message), "Successfully connected to %s", ssid);
    } else {
        result.result = WTERM_ERROR_NETWORK;
        result.error_type = parse_nmcli_error(error_output);
        safe_string_copy(result.error_message, error_output, sizeof(result.error_message));
        result.connected = false;
    }

    return result;
}

connection_result_t connect_to_open_network(const char* ssid) {
    connection_result_t result = {0};

    if (!ssid || is_string_empty(ssid)) {
        result.result = WTERM_ERROR_INVALID_INPUT;
        safe_string_copy(result.error_message, "Invalid SSID provided", sizeof(result.error_message));
        return result;
    }

    // Validate SSID
    if (!validate_ssid(ssid)) {
        result.result = WTERM_ERROR_INVALID_INPUT;
        safe_string_copy(result.error_message, "SSID contains invalid characters or length", sizeof(result.error_message));
        return result;
    }

    // Escape SSID for shell safety
    char escaped_ssid[256];
    if (!shell_escape(ssid, escaped_ssid, sizeof(escaped_ssid))) {
        result.result = WTERM_ERROR_INVALID_INPUT;
        safe_string_copy(result.error_message, "SSID too long for shell escaping", sizeof(result.error_message));
        return result;
    }

    char command[512];
    snprintf(command, sizeof(command), "nmcli device wifi connect %s 2>&1", escaped_ssid);

    return execute_nmcli_connect(command, ssid);
}

connection_result_t connect_to_secured_network(const char* ssid, const char* password) {
    connection_result_t result = {0};

    if (!ssid || is_string_empty(ssid)) {
        result.result = WTERM_ERROR_INVALID_INPUT;
        safe_string_copy(result.error_message, "Invalid SSID provided", sizeof(result.error_message));
        return result;
    }

    if (!password || is_string_empty(password)) {
        result.result = WTERM_ERROR_INVALID_INPUT;
        safe_string_copy(result.error_message, "Password required for secured network", sizeof(result.error_message));
        return result;
    }

    // Validate SSID
    if (!validate_ssid(ssid)) {
        result.result = WTERM_ERROR_INVALID_INPUT;
        safe_string_copy(result.error_message, "SSID contains invalid characters or length", sizeof(result.error_message));
        return result;
    }

    // Escape both SSID and password for shell safety
    char escaped_ssid[256];
    char escaped_password[512];

    if (!shell_escape(ssid, escaped_ssid, sizeof(escaped_ssid))) {
        result.result = WTERM_ERROR_INVALID_INPUT;
        safe_string_copy(result.error_message, "SSID too long for shell escaping", sizeof(result.error_message));
        return result;
    }

    if (!shell_escape(password, escaped_password, sizeof(escaped_password))) {
        result.result = WTERM_ERROR_INVALID_INPUT;
        safe_string_copy(result.error_message, "Password too long for shell escaping", sizeof(result.error_message));
        return result;
    }

    char command[1024];
    snprintf(command, sizeof(command), "nmcli device wifi connect %s password %s 2>&1",
             escaped_ssid, escaped_password);

    return execute_nmcli_connect(command, ssid);
}

connection_status_t get_connection_status(void) {
    connection_status_t status = {0};

    FILE *fp = popen("nmcli -t -f ACTIVE,SSID device wifi list", "r");
    if (!fp) {
        return status;
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), fp)) {
        buffer[strcspn(buffer, "\n")] = '\0';

        // Parse line: ACTIVE:SSID
        if (strncmp(buffer, "yes:", 4) == 0) {
            const char *ssid_start = buffer + 4;
            safe_string_copy(status.connected_ssid, ssid_start, sizeof(status.connected_ssid));
            status.is_connected = true;
            break;
        }
    }

    pclose(fp);

    // Get IP address if connected
    if (status.is_connected) {
        fp = popen("nmcli -t -f IP4.ADDRESS connection show --active 2>/dev/null | head -1", "r");
        if (fp) {
            if (fgets(buffer, sizeof(buffer), fp)) {
                buffer[strcspn(buffer, "\n")] = '\0';
                // Extract IP address (remove prefix like "IP4.ADDRESS[1]:")
                const char *ip_start = strrchr(buffer, ':');
                if (ip_start && ip_start[1] != '\0') {
                    safe_string_copy(status.ip_address, ip_start + 1, sizeof(status.ip_address));
                } else if (strlen(buffer) > 0) {
                    safe_string_copy(status.ip_address, buffer, sizeof(status.ip_address));
                }
            }
            pclose(fp);
        }
    }

    return status;
}

wterm_result_t disconnect_current_network(void) {
    connection_status_t status = get_connection_status();

    if (!status.is_connected) {
        return WTERM_SUCCESS; // Already disconnected
    }

    char command[256];
    snprintf(command, sizeof(command), "nmcli device wifi disconnect");

    int result = system(command);
    return (result == 0) ? WTERM_SUCCESS : WTERM_ERROR_NETWORK;
}

bool network_requires_password(const char* security) {
    if (!security || is_string_empty(security)) {
        return false; // Open network
    }

    // Check for common security types that require passwords
    return (strstr(security, "WPA") != NULL ||
            strstr(security, "WEP") != NULL ||
            strstr(security, "Enterprise") != NULL);
}

bool validate_password(const char* password, const char* security) {
    if (!password) {
        return false;
    }

    size_t len = strlen(password);

    if (!security || is_string_empty(security)) {
        return true; // Open network, no password needed
    }

    // WPA/WPA2 passwords must be 8-63 characters
    if (strstr(security, "WPA")) {
        return (len >= 8 && len <= 63);
    }

    // WEP passwords are typically 5, 13, 16, or 29 characters
    if (strstr(security, "WEP")) {
        return (len == 5 || len == 13 || len == 16 || len == 29);
    }

    // Default: accept any non-empty password
    return (len > 0);
}

void secure_clear_password(char* password, size_t length) {
    if (password) {
        // Securely clear password from memory
        volatile char *p = password;
        for (size_t i = 0; i < length; i++) {
            p[i] = 0;
        }
    }
}

const char* get_signal_bars(const char* signal_str) {
    if (!signal_str) {
        return "    ";
    }

    int signal = atoi(signal_str);

    if (signal >= 80) return "▇▇▇▇▇";
    if (signal >= 60) return "▇▇▇▇▆";
    if (signal >= 40) return "▇▇▇▆▆";
    if (signal >= 20) return "▇▇▆▆▆";
    if (signal > 0)   return "▇▆▆▆▆";

    return "▁▁▁▁▁";
}

bool is_connected_to_network(const char* ssid) {
    if (!ssid) {
        return false;
    }

    connection_status_t status = get_connection_status();
    return (status.is_connected && strcmp(status.connected_ssid, ssid) == 0);
}

connection_result_t monitor_connection_progress(const char* ssid, int timeout_seconds) {
    connection_result_t result = {0};

    if (!ssid) {
        result.result = WTERM_ERROR_INVALID_INPUT;
        return result;
    }

    // Monitor connection for specified timeout
    for (int i = 0; i < timeout_seconds; i++) {
        if (is_connected_to_network(ssid)) {
            result.result = WTERM_SUCCESS;
            result.connected = true;
            snprintf(result.error_message, sizeof(result.error_message),
                    "Successfully connected to %s", ssid);
            return result;
        }

        sleep(1);
    }

    // Timeout reached
    result.result = WTERM_ERROR_NETWORK;
    result.error_type = CONN_ERROR_TIMEOUT;
    result.connected = false;
    snprintf(result.error_message, sizeof(result.error_message),
            "Connection to %s timed out", ssid);

    return result;
}