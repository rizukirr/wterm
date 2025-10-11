/**
 * @file connection.c
 * @brief WiFi connection management implementation
 */

#define _POSIX_C_SOURCE 200809L
#include "connection.h"
#include "error_handler.h"
#include "../utils/string_utils.h"
#include "../utils/input_sanitizer.h"
#include "../utils/safe_exec.h"
#include "../utils/iw_helper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>

// Global cancellation state for connection operations
// Using volatile sig_atomic_t for thread-safety and signal-safety
static volatile sig_atomic_t connection_cancelled = 0;

void init_connection_cancel(void) {
    connection_cancelled = 0;
}

void request_connection_cancel(void) {
    connection_cancelled = 1;
}

bool is_connection_cancelled(void) {
    return connection_cancelled != 0;
}

// Helper function to check if a saved connection exists for the given SSID
static bool connection_exists(const char* ssid) {
    if (!ssid || is_string_empty(ssid)) {
        return false;
    }

    char command[512];
    snprintf(command, sizeof(command), "nmcli -t -f NAME,TYPE connection show");

    FILE *fp = popen(command, "r");
    if (!fp) {
        return false;
    }

    bool exists = false;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), fp)) {
        buffer[strcspn(buffer, "\n")] = '\0';

        // Parse line: NAME:TYPE
        char *colon = strchr(buffer, ':');
        if (colon) {
            *colon = '\0';
            const char *name = buffer;
            const char *type = colon + 1;

            // Check if this is a WiFi connection with matching SSID
            if (strcmp(type, "802-11-wireless") == 0 && strcmp(name, ssid) == 0) {
                exists = true;
                break;
            }
        }
    }

    pclose(fp);
    return exists;
}

// Public function to check if a saved connection exists
bool is_saved_connection(const char* ssid) {
    return connection_exists(ssid);
}

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

    // Always verify the actual connection status, regardless of nmcli exit code
    // This handles cases where nmcli returns error but connection succeeds
    // Wait up to 2 seconds for connection to establish (cancellable)
    struct timespec sleep_time = {0, 100000000};  // 100ms
    for (int i = 0; i < 20 && !is_connection_cancelled(); i++) {
        nanosleep(&sleep_time, NULL);
    }

    // Check if cancelled during initial wait
    if (is_connection_cancelled()) {
        result.result = WTERM_ERROR_CANCELLED;
        safe_string_copy(result.error_message, "Connection cancelled by user", sizeof(result.error_message));
        return result;
    }

    // Get WiFi interface for iw verification
    char wifi_interface[MAX_STR_INTERFACE];
    if (!iw_get_first_wifi_interface(wifi_interface, sizeof(wifi_interface))) {
        safe_string_copy(wifi_interface, "wlan0", sizeof(wifi_interface));
    }

    for (int i = 0; i < 13; i++) {
        // Check for cancellation request
        if (is_connection_cancelled()) {
            result.result = WTERM_ERROR_CANCELLED;
            safe_string_copy(result.error_message, "Connection cancelled by user", sizeof(result.error_message));
            return result;
        }

        connection_status_t status = get_connection_status();

        // Check if we're connected to the target SSID via NetworkManager
        if (status.is_connected && strcmp(status.connected_ssid, ssid) == 0) {
            result.result = WTERM_SUCCESS;
            result.connected = true;
            snprintf(result.error_message, sizeof(result.error_message),
                    "Successfully connected to %s", ssid);
            return result;
        }

        // Fallback: Check kernel-level connection via iw AND verify IP assignment
        // This catches cases where NM reports failure but device is actually connected
        // However, we also need to ensure DHCP completed and IP was assigned
        char iw_ssid[MAX_STR_SSID];
        if (iw_get_connected_ssid(wifi_interface, iw_ssid, sizeof(iw_ssid)) == WTERM_SUCCESS) {
            if (strlen(iw_ssid) > 0 && strcmp(iw_ssid, ssid) == 0) {
                // WiFi is associated, now check if IP address is assigned
                bool has_ip = false;
                if (interface_has_ip_address(wifi_interface, &has_ip) == WTERM_SUCCESS && has_ip) {
                    result.result = WTERM_SUCCESS;
                    result.connected = true;
                    snprintf(result.error_message, sizeof(result.error_message),
                            "Successfully connected to %s", ssid);
                    return result;
                }
                // WiFi associated but no IP yet, keep waiting
            }
        }

        // If connection failed or was deactivated, stop waiting
        // Check if the connection profile still exists and is in failed state
        FILE *check_fp = popen("nmcli -t -f NAME,STATE connection show 2>&1", "r");
        if (check_fp) {
            char check_buffer[256];
            bool connection_failed = false;

            while (fgets(check_buffer, sizeof(check_buffer), check_fp)) {
                check_buffer[strcspn(check_buffer, "\n")] = '\0';

                // Parse NAME:STATE
                char *colon = strchr(check_buffer, ':');
                if (colon) {
                    *colon = '\0';
                    const char *name = check_buffer;
                    const char *state = colon + 1;

                    // Check if this connection is for our SSID and in failed/deactivated state
                    if (strstr(name, ssid) != NULL) {
                        if (strcmp(state, "activated") == 0) {
                            // Connection is activated, but might not be WiFi
                            // Continue waiting for get_connection_status to confirm WiFi
                            break;
                        } else if (strstr(state, "deactivat") != NULL) {
                            connection_failed = true;
                            break;
                        }
                    }
                }
            }
            pclose(check_fp);

            if (connection_failed) {
                result.result = WTERM_ERROR_NETWORK;
                result.error_type = CONN_ERROR_AUTH_FAILED;
                snprintf(result.error_message, sizeof(result.error_message),
                        "Connection to %s failed or deactivated", ssid);
                result.connected = false;
                return result;
            }
        }

        // Sleep for 1 second, but check cancellation every 100ms
        for (int j = 0; j < 10 && !is_connection_cancelled(); j++) {
            nanosleep(&sleep_time, NULL);
        }
    }

    // Connection didn't establish in time
    // If nmcli returned an error, use that message; otherwise report timeout
    if (exit_code != 0 && strlen(error_output) > 0) {
        result.result = WTERM_ERROR_NETWORK;
        result.error_type = parse_nmcli_error(error_output);
        safe_string_copy(result.error_message, error_output, sizeof(result.error_message));
    } else {
        result.result = WTERM_ERROR_NETWORK;
        result.error_type = CONN_ERROR_TIMEOUT;
        snprintf(result.error_message, sizeof(result.error_message),
                "Connection to %s timed out (check signal strength, password, or AP availability)", ssid);
    }
    result.connected = false;

    return result;
}

/**
 * @brief Clean up zombie connections before attempting new connection
 *
 * Detects and forcefully cleans up any kernel-level WiFi associations
 * that NetworkManager is unaware of. This prevents connection failures
 * caused by the kernel already being associated with another network.
 *
 * @return wterm_result_t WTERM_SUCCESS if cleanup successful or no cleanup needed
 */
static wterm_result_t cleanup_zombie_before_connect(void) {
    // Get WiFi interface
    char wifi_interface[MAX_STR_INTERFACE];
    if (!iw_get_first_wifi_interface(wifi_interface, sizeof(wifi_interface))) {
        safe_string_copy(wifi_interface, "wlan0", sizeof(wifi_interface));
    }

    // Check for kernel-level association
    bool kernel_associated = false;
    iw_check_association(wifi_interface, &kernel_associated);

    // If no kernel association, nothing to clean up
    if (!kernel_associated) {
        return WTERM_SUCCESS;
    }

    // Get current NetworkManager status
    connection_status_t status = {0};
    FILE *fp = popen("nmcli -t -f NAME,TYPE,DEVICE connection show --active", "r");
    if (fp) {
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), fp)) {
            buffer[strcspn(buffer, "\n")] = '\0';

            char *first_colon = strchr(buffer, ':');
            if (!first_colon) continue;

            char *second_colon = strchr(first_colon + 1, ':');
            if (!second_colon) continue;

            *first_colon = '\0';
            *second_colon = '\0';
            const char *name = buffer;
            const char *type = first_colon + 1;

            if (strcmp(type, "802-11-wireless") == 0) {
                safe_string_copy(status.connection_name, name, sizeof(status.connection_name));
                status.is_connected = true;
                break;
            }
        }
        pclose(fp);
    }

    // Check if this is actually a zombie connection
    // Zombie = kernel associated but NetworkManager NOT connected
    bool is_zombie = (kernel_associated && !status.is_connected);

    if (!is_zombie) {
        // Not a zombie - kernel and NM are in sync
        return WTERM_SUCCESS;
    }

    // Zombie detected - perform aggressive cleanup
    // Note: User doesn't need to see technical details, just that we're preparing
    fprintf(stderr, "Initializing connection...\n");

    // Check if cancelled before starting cleanup
    if (is_connection_cancelled()) {
        return WTERM_ERROR_CANCELLED;
    }

    // Step 1: Try to disconnect active NetworkManager connection if any
    if (status.is_connected && status.connection_name[0] != '\0') {
        char* const args[] = {
            "nmcli",
            "connection",
            "down",
            status.connection_name,
            NULL
        };
        safe_exec_check_silent("nmcli", args);

        // Cancellable sleep
        struct timespec cleanup_sleep = {0, 100000000};  // 100ms
        for (int i = 0; i < 10 && !is_connection_cancelled(); i++) {
            nanosleep(&cleanup_sleep, NULL);
        }
        if (is_connection_cancelled()) {
            return WTERM_ERROR_CANCELLED;
        }
    }

    // Step 2: Force device-level disconnect (suppressing expected errors)
    pid_t device_pid = fork();
    if (device_pid == 0) {
        // Child: redirect stderr to /dev/null
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        char* const device_args[] = {
            "nmcli",
            "device",
            "disconnect",
            (char*)wifi_interface,
            NULL
        };
        execvp("nmcli", device_args);
        _exit(127);
    } else if (device_pid > 0) {
        int status;
        waitpid(device_pid, &status, 0);
    }

    // Cancellable sleep
    struct timespec cleanup_sleep = {0, 100000000};  // 100ms
    for (int i = 0; i < 10 && !is_connection_cancelled(); i++) {
        nanosleep(&cleanup_sleep, NULL);
    }
    if (is_connection_cancelled()) {
        return WTERM_ERROR_CANCELLED;
    }

    // Step 3: Check if still associated at kernel level
    bool still_associated = false;
    iw_check_association(wifi_interface, &still_associated);

    // Step 4: If still associated, force device reset via management toggle
    if (still_associated) {
        char* const unmanage_args[] = {
            "nmcli",
            "device",
            "set",
            (char*)wifi_interface,
            "managed",
            "no",
            NULL
        };
        safe_exec_check_silent("nmcli", unmanage_args);

        // Cancellable sleep
        for (int i = 0; i < 10 && !is_connection_cancelled(); i++) {
            nanosleep(&cleanup_sleep, NULL);
        }
        if (is_connection_cancelled()) {
            return WTERM_ERROR_CANCELLED;
        }

        char* const manage_args[] = {
            "nmcli",
            "device",
            "set",
            (char*)wifi_interface,
            "managed",
            "yes",
            NULL
        };
        safe_exec_check_silent("nmcli", manage_args);

        // Cancellable sleep (2 seconds = 20 * 100ms)
        for (int i = 0; i < 20 && !is_connection_cancelled(); i++) {
            nanosleep(&cleanup_sleep, NULL);
        }
        if (is_connection_cancelled()) {
            return WTERM_ERROR_CANCELLED;
        }
    }

    // Final verification
    iw_check_association(wifi_interface, &still_associated);
    if (still_associated) {
        // Cleanup failed, but continue anyway - nmcli might still work
        return WTERM_ERROR_NETWORK;
    }

    return WTERM_SUCCESS;
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

    // Initialize cancellation state for this connection attempt
    init_connection_cancel();

    // Escape SSID for shell safety
    char escaped_ssid[256];
    if (!shell_escape(ssid, escaped_ssid, sizeof(escaped_ssid))) {
        result.result = WTERM_ERROR_INVALID_INPUT;
        safe_string_copy(result.error_message, "SSID too long for shell escaping", sizeof(result.error_message));
        return result;
    }

    // Clean up any zombie connections before attempting new connection
    wterm_result_t cleanup_result = cleanup_zombie_before_connect();
    if (cleanup_result != WTERM_SUCCESS) {
        result.result = WTERM_ERROR_NETWORK;
        safe_string_copy(result.error_message, "Failed to cleanup zombie connection before connecting", sizeof(result.error_message));
        return result;
    }

    char command[512];

    // Check if a saved connection exists for this SSID
    if (connection_exists(ssid)) {
        // Use 'nmcli connection up' for existing connections
        snprintf(command, sizeof(command), "nmcli connection up %s 2>&1", escaped_ssid);
    } else {
        // Use 'nmcli device wifi connect' for new connections
        snprintf(command, sizeof(command), "nmcli device wifi connect %s 2>&1", escaped_ssid);
    }

    return execute_nmcli_connect(command, ssid);
}

connection_result_t connect_to_secured_network(const char* ssid, const char* password) {
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

    // Initialize cancellation state for this connection attempt
    init_connection_cancel();

    // Escape SSID for shell safety
    char escaped_ssid[256];
    if (!shell_escape(ssid, escaped_ssid, sizeof(escaped_ssid))) {
        result.result = WTERM_ERROR_INVALID_INPUT;
        safe_string_copy(result.error_message, "SSID too long for shell escaping", sizeof(result.error_message));
        return result;
    }

    // Clean up any zombie connections before attempting new connection
    wterm_result_t cleanup_result = cleanup_zombie_before_connect();
    if (cleanup_result != WTERM_SUCCESS) {
        result.result = WTERM_ERROR_NETWORK;
        safe_string_copy(result.error_message, "Failed to cleanup zombie connection before connecting", sizeof(result.error_message));
        return result;
    }

    char command[1024];

    // Check if a saved connection exists for this SSID
    if (connection_exists(ssid)) {
        // Use 'nmcli connection up' for existing connections
        // Password is stored in the saved connection, so we don't need to pass it
        snprintf(command, sizeof(command), "nmcli connection up %s 2>&1", escaped_ssid);
    } else {
        // New connection - password is required
        if (!password || is_string_empty(password)) {
            result.result = WTERM_ERROR_INVALID_INPUT;
            safe_string_copy(result.error_message, "Password required for secured network", sizeof(result.error_message));
            return result;
        }

        // Escape password for shell safety
        char escaped_password[512];
        if (!shell_escape(password, escaped_password, sizeof(escaped_password))) {
            result.result = WTERM_ERROR_INVALID_INPUT;
            safe_string_copy(result.error_message, "Password too long for shell escaping", sizeof(result.error_message));
            return result;
        }

        // Use 'nmcli device wifi connect' for new connections
        snprintf(command, sizeof(command), "nmcli device wifi connect %s password %s 2>&1",
                 escaped_ssid, escaped_password);
    }

    return execute_nmcli_connect(command, ssid);
}

connection_status_t get_connection_status(void) {
    connection_status_t status = {0};

    // Get active connection profile name for WiFi
    FILE *fp = popen("nmcli -t -f NAME,TYPE,DEVICE connection show --active", "r");
    if (!fp) {
        return status;
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), fp)) {
        buffer[strcspn(buffer, "\n")] = '\0';

        // Parse line: NAME:TYPE:DEVICE
        char *first_colon = strchr(buffer, ':');
        if (!first_colon) continue;

        char *second_colon = strchr(first_colon + 1, ':');
        if (!second_colon) continue;

        // Extract fields
        *first_colon = '\0';
        *second_colon = '\0';
        const char *name = buffer;
        const char *type = first_colon + 1;

        // Check if this is a WiFi connection
        if (strcmp(type, "802-11-wireless") == 0) {
            safe_string_copy(status.connection_name, name, sizeof(status.connection_name));
            status.is_connected = true;
            break;
        }
    }

    pclose(fp);

    // Get SSID from device wifi list if connected
    if (status.is_connected) {
        fp = popen("nmcli -t -f ACTIVE,SSID device wifi list", "r");
        if (fp) {
            while (fgets(buffer, sizeof(buffer), fp)) {
                buffer[strcspn(buffer, "\n")] = '\0';

                // Parse line: ACTIVE:SSID
                if (strncmp(buffer, "yes:", 4) == 0) {
                    const char *ssid_start = buffer + 4;
                    safe_string_copy(status.connected_ssid, ssid_start, sizeof(status.connected_ssid));
                    break;
                }
            }
            pclose(fp);
        }
    }

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

    // Check kernel-level association state to detect zombie connections
    char wifi_interface[MAX_STR_INTERFACE];
    if (!iw_get_first_wifi_interface(wifi_interface, sizeof(wifi_interface))) {
        // Fallback to wlan0 if detection fails
        safe_string_copy(wifi_interface, "wlan0", sizeof(wifi_interface));
    }

    bool kernel_associated = false;
    iw_check_association(wifi_interface, &kernel_associated);
    status.kernel_associated = kernel_associated;

    // Get SSID from kernel if associated
    if (kernel_associated) {
        iw_get_connected_ssid(wifi_interface, status.kernel_ssid, sizeof(status.kernel_ssid));
    }

    // Detect zombie connection: kernel connected but NetworkManager disconnected
    status.is_zombie = (kernel_associated && !status.is_connected);

    // Auto-recover from zombie state
    if (status.is_zombie) {
        // Attempt recovery by forcing NetworkManager to re-sync
        if (recover_from_zombie_connection(wifi_interface) == WTERM_SUCCESS) {
            // Re-check connection status after recovery
            fp = popen("nmcli -t -f NAME,TYPE,DEVICE connection show --active", "r");
            if (fp) {
                while (fgets(buffer, sizeof(buffer), fp)) {
                    buffer[strcspn(buffer, "\n")] = '\0';

                    char *first_colon = strchr(buffer, ':');
                    if (!first_colon) continue;

                    char *second_colon = strchr(first_colon + 1, ':');
                    if (!second_colon) continue;

                    *first_colon = '\0';
                    *second_colon = '\0';
                    const char *name = buffer;
                    const char *type = first_colon + 1;

                    if (strcmp(type, "802-11-wireless") == 0) {
                        safe_string_copy(status.connection_name, name, sizeof(status.connection_name));
                        status.is_connected = true;
                        status.is_zombie = false;
                        break;
                    }
                }
                pclose(fp);
            }

            // Get SSID after recovery
            if (status.is_connected) {
                fp = popen("nmcli -t -f ACTIVE,SSID device wifi list", "r");
                if (fp) {
                    while (fgets(buffer, sizeof(buffer), fp)) {
                        buffer[strcspn(buffer, "\n")] = '\0';
                        if (strncmp(buffer, "yes:", 4) == 0) {
                            const char *ssid_start = buffer + 4;
                            safe_string_copy(status.connected_ssid, ssid_start, sizeof(status.connected_ssid));
                            break;
                        }
                    }
                    pclose(fp);
                }

                // Get IP address after recovery
                fp = popen("nmcli -t -f IP4.ADDRESS connection show --active 2>/dev/null | head -1", "r");
                if (fp) {
                    if (fgets(buffer, sizeof(buffer), fp)) {
                        buffer[strcspn(buffer, "\n")] = '\0';
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
        }
    }

    return status;
}

wterm_result_t disconnect_current_network(void) {
    connection_status_t status = get_connection_status();

    // Get the WiFi interface dynamically
    char wifi_interface[MAX_STR_INTERFACE];
    if (!iw_get_first_wifi_interface(wifi_interface, sizeof(wifi_interface))) {
        // Fallback to wlan0 if detection fails
        safe_string_copy(wifi_interface, "wlan0", sizeof(wifi_interface));
    }

    // Try to disconnect active NetworkManager connection if one exists
    if (status.is_connected && status.connection_name[0] != '\0') {
        char* const args[] = {
            "nmcli",
            "connection",
            "down",
            status.connection_name,
            NULL
        };

        safe_exec_check_silent("nmcli", args);
        // Don't return yet - need to check for zombie connections
    }

    // Also try device-level disconnect to handle zombie connections
    // This ensures we disconnect even if NetworkManager has no active connection profile
    // but the WiFi device still has a kernel-level association
    // Note: This may fail if NM already considers the device disconnected (expected for zombies)
    // We suppress stderr to avoid confusing error messages for expected failures
    pid_t device_pid = fork();
    if (device_pid == 0) {
        // Child: redirect stderr to /dev/null to suppress expected error messages
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        char* const device_args[] = {
            "nmcli",
            "device",
            "disconnect",
            (char*)wifi_interface,
            NULL
        };
        execvp("nmcli", device_args);
        _exit(127);
    } else if (device_pid > 0) {
        // Parent: wait for child but ignore result (failure is expected for zombies)
        int status;
        waitpid(device_pid, &status, 0);
    }

    // Verify disconnection by checking if any WiFi association remains at kernel level
    sleep(1); // Give NetworkManager time to process

    bool still_associated = false;
    iw_check_association(wifi_interface, &still_associated);

    if (still_associated) {
        // Zombie connection detected - force device reset
        // Set device unmanaged then managed again to force cleanup
        char* const unmanage_args[] = {
            "nmcli",
            "device",
            "set",
            (char*)wifi_interface,
            "managed",
            "no",
            NULL
        };
        safe_exec_check("nmcli", unmanage_args);

        sleep(1);

        char* const manage_args[] = {
            "nmcli",
            "device",
            "set",
            (char*)wifi_interface,
            "managed",
            "yes",
            NULL
        };
        safe_exec_check("nmcli", manage_args);

        sleep(1);
    }

    // Final verification
    connection_status_t final_status = get_connection_status();
    if (final_status.is_connected) {
        return WTERM_ERROR_NETWORK; // Still connected somehow
    }

    // Check kernel level one more time
    iw_check_association(wifi_interface, &still_associated);
    if (still_associated) {
        return WTERM_ERROR_NETWORK; // Kernel-level association still exists
    }

    return WTERM_SUCCESS;
}

wterm_result_t recover_from_zombie_connection(const char* interface) {
    if (!interface || strlen(interface) == 0) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    // Force NetworkManager to re-sync with kernel state
    // by toggling interface management off and on
    char* const unmanage_args[] = {
        "nmcli",
        "device",
        "set",
        (char*)interface,
        "managed",
        "no",
        NULL
    };

    wterm_result_t result = safe_exec_check("nmcli", unmanage_args);
    if (result != WTERM_SUCCESS) {
        return result;
    }

    sleep(1);

    char* const manage_args[] = {
        "nmcli",
        "device",
        "set",
        (char*)interface,
        "managed",
        "yes",
        NULL
    };

    result = safe_exec_check("nmcli", manage_args);
    if (result != WTERM_SUCCESS) {
        return result;
    }

    // Give NetworkManager time to re-detect the connection
    sleep(2);

    return WTERM_SUCCESS;
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