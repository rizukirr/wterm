/**
 * @file error_handler.c
 * @brief WiFi connection error detection and handling implementation
 */

#define _POSIX_C_SOURCE 200809L
#include "error_handler.h"
#include "../utils/string_utils.h"
#include "../utils/safe_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <fcntl.h>

connection_error_t parse_nmcli_error(const char* error_output) {
    if (!error_output) {
        return CONN_ERROR_UNKNOWN;
    }

    // Convert to lowercase for easier matching
    char error_lower[512];
    safe_string_copy(error_lower, error_output, sizeof(error_lower));

    // Convert to lowercase using standard approach
    for (int i = 0; error_lower[i]; i++) {
        error_lower[i] = tolower((unsigned char)error_lower[i]);
    }

    // Check for specific error patterns
    if (strstr(error_lower, "authentication") || strstr(error_lower, "invalid key") ||
        strstr(error_lower, "wrong password") || strstr(error_lower, "psk")) {
        return CONN_ERROR_AUTH_FAILED;
    }

    if (strstr(error_lower, "no network") || strstr(error_lower, "not found") ||
        strstr(error_lower, "unavailable")) {
        return CONN_ERROR_NETWORK_UNAVAILABLE;
    }

    if (strstr(error_lower, "timeout") || strstr(error_lower, "timed out")) {
        return CONN_ERROR_TIMEOUT;
    }

    if (strstr(error_lower, "permission denied") || strstr(error_lower, "not authorized")) {
        return CONN_ERROR_PERMISSION_DENIED;
    }

    if (strstr(error_lower, "wifi") && (strstr(error_lower, "disabled") || strstr(error_lower, "off"))) {
        return CONN_ERROR_WIFI_DISABLED;
    }

    if (strstr(error_lower, "networkmanager") || strstr(error_lower, "nm-")) {
        return CONN_ERROR_NETWORKMANAGER_NOT_RUNNING;
    }

    if (strstr(error_lower, "dhcp") || strstr(error_lower, "ip address")) {
        return CONN_ERROR_DHCP_TIMEOUT;
    }

    return CONN_ERROR_UNKNOWN;
}

error_info_t get_error_info(connection_error_t error, const char* network_name) {
    error_info_t info = {0};
    info.type = error;

    switch (error) {
        case CONN_ERROR_AUTH_FAILED:
            snprintf(info.message, sizeof(info.message),
                    "Authentication failed for '%s'", network_name ? network_name : "network");
            safe_string_copy(info.suggestion,
                    "Check the password and try again. Press 'r' to retry with new password.",
                    sizeof(info.suggestion));
            info.can_retry = true;
            info.auto_fixable = false;
            break;

        case CONN_ERROR_NETWORK_UNAVAILABLE:
            snprintf(info.message, sizeof(info.message),
                    "Network '%s' is no longer available", network_name ? network_name : "unknown");
            safe_string_copy(info.suggestion,
                    "Network may be out of range. Press 'r' to refresh the list.",
                    sizeof(info.suggestion));
            info.can_retry = true;
            info.auto_fixable = false;
            break;

        case CONN_ERROR_WIFI_DISABLED:
            safe_string_copy(info.message, "WiFi adapter is disabled", sizeof(info.message));
            safe_string_copy(info.suggestion,
                    "Press 'e' to enable WiFi adapter, or check hardware switch.",
                    sizeof(info.suggestion));
            info.can_retry = true;
            info.auto_fixable = true;
            break;

        case CONN_ERROR_PERMISSION_DENIED:
            safe_string_copy(info.message, "Permission denied", sizeof(info.message));
            safe_string_copy(info.suggestion,
                    "Try running with sudo: sudo wterm --tui",
                    sizeof(info.suggestion));
            info.can_retry = false;
            info.auto_fixable = false;
            break;

        case CONN_ERROR_NETWORKMANAGER_NOT_RUNNING:
            safe_string_copy(info.message, "NetworkManager service not running", sizeof(info.message));
            safe_string_copy(info.suggestion,
                    "Start NetworkManager: sudo systemctl start NetworkManager",
                    sizeof(info.suggestion));
            info.can_retry = false;
            info.auto_fixable = false;
            break;

        case CONN_ERROR_TIMEOUT:
            snprintf(info.message, sizeof(info.message),
                    "Connection to '%s' timed out", network_name ? network_name : "network");
            safe_string_copy(info.suggestion,
                    "Network may be slow or overloaded. Press 'r' to retry.",
                    sizeof(info.suggestion));
            info.can_retry = true;
            info.auto_fixable = false;
            break;

        case CONN_ERROR_DHCP_TIMEOUT:
            snprintf(info.message, sizeof(info.message),
                    "Connected to '%s' but no IP address received", network_name ? network_name : "network");
            safe_string_copy(info.suggestion,
                    "DHCP server may be unavailable. Press 'r' to retry.",
                    sizeof(info.suggestion));
            info.can_retry = true;
            info.auto_fixable = false;
            break;

        default:
            safe_string_copy(info.message, "Unknown connection error", sizeof(info.message));
            safe_string_copy(info.suggestion,
                    "Check network settings and try again.",
                    sizeof(info.suggestion));
            info.can_retry = true;
            info.auto_fixable = false;
            break;
    }

    return info;
}

bool is_wifi_enabled(void) {
    FILE *fp = popen("nmcli radio wifi", "r");
    if (!fp) {
        return false;
    }

    char buffer[32];
    bool enabled = false;
    if (fgets(buffer, sizeof(buffer), fp)) {
        // Remove newline
        buffer[strcspn(buffer, "\n")] = '\0';
        enabled = (strcmp(buffer, "enabled") == 0);
    }

    pclose(fp);
    return enabled;
}

bool is_networkmanager_running(void) {
    char* const args[] = {
        "systemctl",
        "is-active",
        "--quiet",
        "NetworkManager",
        NULL
    };
    return safe_exec_check("systemctl", args);
}

bool test_internet_connectivity(void) {
    // Test with a quick ping to Google DNS
    char* const args[] = {
        "ping",
        "-c",
        "1",
        "-W",
        "2",
        "8.8.8.8",
        NULL
    };

    pid_t pid = fork();
    if (pid < 0) {
        return false;
    }

    if (pid == 0) {
        // Child process - redirect output to /dev/null
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        execvp("ping", args);
        _exit(127);
    }

    // Parent process
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        return false;
    }

    return (WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

bool auto_enable_wifi(void) {
    if (is_wifi_enabled()) {
        return true; // Already enabled
    }

    char* const args[] = {
        "nmcli",
        "radio",
        "wifi",
        "on",
        NULL
    };
    return safe_exec_check("nmcli", args);
}

const char* get_error_message(connection_error_t error) {
    static char error_msg[256];
    error_info_t info = get_error_info(error, NULL);
    safe_string_copy(error_msg, info.message, sizeof(error_msg));
    return error_msg;
}

const char* get_error_suggestion(connection_error_t error) {
    static char suggestion_msg[512];
    error_info_t info = get_error_info(error, NULL);
    safe_string_copy(suggestion_msg, info.suggestion, sizeof(suggestion_msg));
    return suggestion_msg;
}

bool can_auto_fix_error(connection_error_t error) {
    error_info_t info = get_error_info(error, NULL);
    return info.auto_fixable;
}