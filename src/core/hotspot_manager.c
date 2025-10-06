/**
 * @file hotspot_manager.c
 * @brief WiFi hotspot management implementation for wterm
 */

#define _POSIX_C_SOURCE 200809L
#include "hotspot_manager.h"
#include "error_handler.h"
#include "../utils/string_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

// Configuration file path
#define HOTSPOT_CONFIG_DIR "/tmp/wterm_hotspots"
#define HOTSPOT_CONFIG_EXT ".conf"
#define HOTSPOT_RUNTIME_DIR "/tmp/wterm_hotspot_runtime"

// Global state
static bool manager_initialized = false;
static hotspot_list_t saved_configs = {0};

// Helper function declarations
static wterm_result_t ensure_directories_exist(void);
static wterm_result_t load_all_configs(void);
static char *get_config_file_path(const char *name);
static wterm_result_t write_config_file(const char *file_path, const hotspot_config_t *config);
static wterm_result_t execute_nmcli_command(const char *command, char *output, size_t output_size);

wterm_result_t hotspot_manager_init(void) {
    if (manager_initialized) {
        return WTERM_SUCCESS;
    }

    wterm_result_t result = ensure_directories_exist();
    if (result != WTERM_SUCCESS) {
        return result;
    }

    result = load_all_configs();
    if (result != WTERM_SUCCESS) {
        return result;
    }

    manager_initialized = true;
    return WTERM_SUCCESS;
}

void hotspot_manager_cleanup(void) {
    if (!manager_initialized) {
        return;
    }

    // Stop all running hotspots
    hotspot_stop(NULL);

    // Clear saved configurations
    memset(&saved_configs, 0, sizeof(saved_configs));

    manager_initialized = false;
}

wterm_result_t hotspot_create_config(const hotspot_config_t *config) {
    if (!manager_initialized) {
        return WTERM_ERROR_GENERAL;
    }

    if (!config || !config->name[0] || !config->ssid[0]) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    // Validate configuration
    wterm_result_t validation_result = hotspot_validate_config(config, NULL, 0);
    if (validation_result != WTERM_SUCCESS) {
        return validation_result;
    }

    // Check if configuration already exists
    for (int i = 0; i < saved_configs.count; i++) {
        if (strcmp(saved_configs.hotspots[i].name, config->name) == 0) {
            return WTERM_ERROR_GENERAL; // Configuration already exists
        }
    }

    // Add to saved configurations
    if (saved_configs.count >= MAX_HOTSPOTS) {
        return WTERM_ERROR_MEMORY;
    }

    memcpy(&saved_configs.hotspots[saved_configs.count], config, sizeof(hotspot_config_t));
    saved_configs.count++;

    // Save to file
    return hotspot_save_config_to_file(config);
}

wterm_result_t hotspot_start(const char *name, hotspot_status_t *status) {
    if (!manager_initialized || !name) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    // Find configuration
    hotspot_config_t *config = NULL;
    for (int i = 0; i < saved_configs.count; i++) {
        if (strcmp(saved_configs.hotspots[i].name, name) == 0) {
            config = &saved_configs.hotspots[i];
            break;
        }
    }

    if (!config) {
        return WTERM_ERROR_GENERAL; // Configuration not found
    }

    // Build nmcli command for hotspot creation using compatible syntax
    char command[2048];  // Increased buffer size

    // Build complete command with all necessary parameters
    int written;
    const char *ipv4_config = (config->share_method == HOTSPOT_SHARE_NONE) ?
        "ipv4.method auto" : "ipv4.method shared ipv4.addresses 192.168.12.1/24";

    if (config->password[0] != '\0') {
        // Secured hotspot with password
        written = snprintf(command, sizeof(command),
            "nmcli connection add type wifi ifname %s con-name %s ssid %s "
            "802-11-wireless.mode ap "
            "802-11-wireless-security.key-mgmt wpa-psk "
            "802-11-wireless-security.psk \"%s\" "
            "%s",
            config->wifi_interface, config->name, config->ssid, config->password, ipv4_config);
    } else {
        // Open hotspot without password
        written = snprintf(command, sizeof(command),
            "nmcli connection add type wifi ifname %s con-name %s ssid %s "
            "802-11-wireless.mode ap "
            "%s",
            config->wifi_interface, config->name, config->ssid, ipv4_config);
    }

    if (written >= (int)sizeof(command)) {
        return WTERM_ERROR_GENERAL;
    }

    // Execute command
    char output[512];
    wterm_result_t result = execute_nmcli_command(command, output, sizeof(output));
    if (result != WTERM_SUCCESS) {
        return result;
    }

    // Start the connection
    snprintf(command, sizeof(command), "nmcli connection up %s", config->name);
    result = execute_nmcli_command(command, output, sizeof(output));
    if (result != WTERM_SUCCESS) {
        return result;
    }

    // Update status if requested
    if (status) {
        memcpy(&status->config, config, sizeof(hotspot_config_t));
        status->state = HOTSPOT_STATE_ACTIVE;
        safe_string_copy(status->status_message, "Hotspot started successfully",
                        sizeof(status->status_message));
        status->client_count = 0;
        status->uptime_seconds = 0;
        status->process_id = 0; // nmcli manages the process
        status->is_persistent = true;
    }

    return WTERM_SUCCESS;
}

wterm_result_t hotspot_stop(const char *name) {
    if (!manager_initialized) {
        return WTERM_ERROR_GENERAL;
    }

    char command[256];
    if (name) {
        // Stop specific hotspot
        snprintf(command, sizeof(command), "nmcli connection down %s", name);
    } else {
        // Stop all active wifi connections in AP mode
        snprintf(command, sizeof(command),
                "nmcli -t -f NAME,TYPE connection show --active | grep wifi | cut -d: -f1 | xargs -I {} nmcli connection down {}");
    }

    char output[512];
    return execute_nmcli_command(command, output, sizeof(output));
}

wterm_result_t hotspot_get_status(const char *name, hotspot_status_t *status) {
    if (!manager_initialized || !name || !status) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    // Find configuration
    hotspot_config_t *config = NULL;
    for (int i = 0; i < saved_configs.count; i++) {
        if (strcmp(saved_configs.hotspots[i].name, name) == 0) {
            config = &saved_configs.hotspots[i];
            break;
        }
    }

    if (!config) {
        return WTERM_ERROR_GENERAL;
    }

    // Check if hotspot is active
    char command[256];
    snprintf(command, sizeof(command),
            "nmcli -t -f NAME connection show --active | grep -q '^%s$'", name);

    char output[512];
    wterm_result_t result = execute_nmcli_command(command, output, sizeof(output));

    memcpy(&status->config, config, sizeof(hotspot_config_t));

    if (result == WTERM_SUCCESS) {
        status->state = HOTSPOT_STATE_ACTIVE;
        safe_string_copy(status->status_message, "Hotspot is running",
                        sizeof(status->status_message));
    } else {
        status->state = HOTSPOT_STATE_STOPPED;
        safe_string_copy(status->status_message, "Hotspot is stopped",
                        sizeof(status->status_message));
    }

    status->client_count = 0; // Will be populated by hotspot_get_clients
    status->uptime_seconds = 0;
    status->process_id = 0;
    status->is_persistent = true;

    return WTERM_SUCCESS;
}

wterm_result_t hotspot_list_configs(hotspot_list_t *list) {
    if (!manager_initialized || !list) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    memcpy(list, &saved_configs, sizeof(hotspot_list_t));
    return WTERM_SUCCESS;
}

wterm_result_t hotspot_delete_config(const char *name) {
    if (!manager_initialized || !name) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    // Stop hotspot if running
    hotspot_stop(name);

    // Remove from saved configurations
    int found_index = -1;
    for (int i = 0; i < saved_configs.count; i++) {
        if (strcmp(saved_configs.hotspots[i].name, name) == 0) {
            found_index = i;
            break;
        }
    }

    if (found_index == -1) {
        return WTERM_ERROR_GENERAL; // Configuration not found
    }

    // Shift remaining configurations
    for (int i = found_index; i < saved_configs.count - 1; i++) {
        memcpy(&saved_configs.hotspots[i], &saved_configs.hotspots[i + 1],
               sizeof(hotspot_config_t));
    }
    saved_configs.count--;

    // Delete configuration file
    char *config_path = get_config_file_path(name);
    if (config_path) {
        unlink(config_path);
        free(config_path);
    }

    // Delete nmcli connection
    char command[256];
    snprintf(command, sizeof(command), "nmcli connection delete %s", name);
    char output[512];
    execute_nmcli_command(command, output, sizeof(output)); // Ignore errors

    return WTERM_SUCCESS;
}

wterm_result_t hotspot_validate_config(const hotspot_config_t *config,
                                       char *error_msg, size_t error_msg_size) {
    if (!config) {
        if (error_msg) {
            safe_string_copy(error_msg, "Configuration is NULL", error_msg_size);
        }
        return WTERM_ERROR_INVALID_INPUT;
    }

    // Validate name
    if (!config->name[0]) {
        if (error_msg) {
            safe_string_copy(error_msg, "Hotspot name cannot be empty", error_msg_size);
        }
        return WTERM_ERROR_INVALID_INPUT;
    }

    // Validate SSID
    if (!config->ssid[0]) {
        if (error_msg) {
            safe_string_copy(error_msg, "SSID cannot be empty", error_msg_size);
        }
        return WTERM_ERROR_INVALID_INPUT;
    }

    if (strlen(config->ssid) > 32) {
        if (error_msg) {
            safe_string_copy(error_msg, "SSID cannot be longer than 32 characters", error_msg_size);
        }
        return WTERM_ERROR_INVALID_INPUT;
    }

    // Validate password
    if (config->security_type != WIFI_SECURITY_NONE) {
        if (!config->password[0]) {
            if (error_msg) {
                safe_string_copy(error_msg, "Password required for secured network", error_msg_size);
            }
            return WTERM_ERROR_INVALID_INPUT;
        }

        if (strlen(config->password) < 8 || strlen(config->password) > 63) {
            if (error_msg) {
                safe_string_copy(error_msg, "Password must be 8-63 characters long", error_msg_size);
            }
            return WTERM_ERROR_INVALID_INPUT;
        }
    }

    // Validate WiFi interface
    if (!config->wifi_interface[0]) {
        if (error_msg) {
            safe_string_copy(error_msg, "WiFi interface must be specified", error_msg_size);
        }
        return WTERM_ERROR_INVALID_INPUT;
    }

    // Validate channel
    if (config->channel < 0 || config->channel > 165) {
        if (error_msg) {
            safe_string_copy(error_msg, "Invalid WiFi channel", error_msg_size);
        }
        return WTERM_ERROR_INVALID_INPUT;
    }

    return WTERM_SUCCESS;
}

void hotspot_get_default_config(hotspot_config_t *config) {
    if (!config) {
        return;
    }

    memset(config, 0, sizeof(hotspot_config_t));

    safe_string_copy(config->name, "wterm_hotspot", sizeof(config->name));
    safe_string_copy(config->ssid, "wterm_hotspot", sizeof(config->ssid));
    safe_string_copy(config->wifi_interface, "wlan0", sizeof(config->wifi_interface));
    safe_string_copy(config->internet_interface, "eth0", sizeof(config->internet_interface));
    safe_string_copy(config->gateway_ip, "192.168.12.1", sizeof(config->gateway_ip));

    config->security_type = WIFI_SECURITY_WPA2;
    config->share_method = HOTSPOT_SHARE_NAT;
    config->channel = 6; // Default 2.4GHz channel
    config->hidden = false;
    config->client_isolation = false;
    config->mac_filtering = false;
    config->is_5ghz = false;
}

// Helper function implementations
static wterm_result_t ensure_directories_exist(void) {
    struct stat st = {0};

    if (stat(HOTSPOT_CONFIG_DIR, &st) == -1) {
        if (mkdir(HOTSPOT_CONFIG_DIR, 0755) != 0) {
            return WTERM_ERROR_GENERAL;
        }
    }

    if (stat(HOTSPOT_RUNTIME_DIR, &st) == -1) {
        if (mkdir(HOTSPOT_RUNTIME_DIR, 0755) != 0) {
            return WTERM_ERROR_GENERAL;
        }
    }

    return WTERM_SUCCESS;
}

static wterm_result_t load_all_configs(void) {
    // Implementation would scan HOTSPOT_CONFIG_DIR and load configurations
    // For now, start with empty configuration list
    saved_configs.count = 0;
    return WTERM_SUCCESS;
}

static char *get_config_file_path(const char *name) {
    if (!name) {
        return NULL;
    }

    size_t path_len = strlen(HOTSPOT_CONFIG_DIR) + strlen(name) + strlen(HOTSPOT_CONFIG_EXT) + 2;
    char *path = malloc(path_len);
    if (!path) {
        return NULL;
    }

    snprintf(path, path_len, "%s/%s%s", HOTSPOT_CONFIG_DIR, name, HOTSPOT_CONFIG_EXT);
    return path;
}

static wterm_result_t execute_nmcli_command(const char *command, char *output, size_t output_size) {
    if (!command) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    FILE *fp = popen(command, "r");
    if (!fp) {
        return WTERM_ERROR_NETWORK;
    }

    if (output && output_size > 0) {
        // Read the first line of output (could be error message)
        if (fgets(output, output_size, fp) == NULL) {
            output[0] = '\0';
        } else {
            // Remove trailing newline for cleaner error messages
            output[strcspn(output, "\n")] = '\0';
        }
    }

    int status = pclose(fp);
    if (status != 0) {
        // Print the nmcli error to help with debugging
        if (output && output[0] != '\0') {
            fprintf(stderr, "nmcli error: %s\n", output);
        }
        return WTERM_ERROR_NETWORK;
    }

    return WTERM_SUCCESS;
}

wterm_result_t hotspot_save_config_to_file(const hotspot_config_t *config) {
    if (!config) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    char *config_path = get_config_file_path(config->name);
    if (!config_path) {
        return WTERM_ERROR_MEMORY;
    }

    wterm_result_t result = write_config_file(config_path, config);
    free(config_path);

    return result;
}

static wterm_result_t write_config_file(const char *file_path, const hotspot_config_t *config) {
    FILE *fp = fopen(file_path, "w");
    if (!fp) {
        return WTERM_ERROR_GENERAL;
    }

    fprintf(fp, "name=%s\n", config->name);
    fprintf(fp, "ssid=%s\n", config->ssid);
    fprintf(fp, "password=%s\n", config->password);
    fprintf(fp, "wifi_interface=%s\n", config->wifi_interface);
    fprintf(fp, "internet_interface=%s\n", config->internet_interface);
    fprintf(fp, "gateway_ip=%s\n", config->gateway_ip);
    fprintf(fp, "security_type=%d\n", config->security_type);
    fprintf(fp, "share_method=%d\n", config->share_method);
    fprintf(fp, "channel=%d\n", config->channel);
    fprintf(fp, "hidden=%d\n", config->hidden ? 1 : 0);
    fprintf(fp, "client_isolation=%d\n", config->client_isolation ? 1 : 0);
    fprintf(fp, "mac_filtering=%d\n", config->mac_filtering ? 1 : 0);
    fprintf(fp, "is_5ghz=%d\n", config->is_5ghz ? 1 : 0);

    fclose(fp);
    return WTERM_SUCCESS;
}

// Utility functions for string conversion
const char *hotspot_security_type_to_string(wifi_security_t security_type) {
    switch (security_type) {
        case WIFI_SECURITY_NONE: return "None";
        case WIFI_SECURITY_WEP: return "WEP";
        case WIFI_SECURITY_WPA: return "WPA";
        case WIFI_SECURITY_WPA2: return "WPA2";
        case WIFI_SECURITY_WPA3: return "WPA3";
        case WIFI_SECURITY_WPA_WPA2: return "WPA/WPA2";
        case WIFI_SECURITY_ENTERPRISE: return "Enterprise";
        default: return "Unknown";
    }
}

const char *hotspot_share_method_to_string(hotspot_share_method_t share_method) {
    switch (share_method) {
        case HOTSPOT_SHARE_NONE: return "None";
        case HOTSPOT_SHARE_NAT: return "NAT";
        case HOTSPOT_SHARE_BRIDGE: return "Bridge";
        default: return "Unknown";
    }
}

wterm_result_t hotspot_quick_start(const char *ssid, const char *password,
                                   const char *wifi_interface,
                                   const char *internet_interface,
                                   hotspot_status_t *status) {
    if (!ssid || !wifi_interface) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    wterm_result_t result = hotspot_manager_init();
    if (result != WTERM_SUCCESS) {
        return result;
    }

    // Create a quick configuration
    hotspot_config_t config;
    hotspot_get_default_config(&config);

    // Override with provided values
    safe_string_copy(config.name, "wterm_quick", sizeof(config.name));
    safe_string_copy(config.ssid, ssid, sizeof(config.ssid));
    if (password) {
        safe_string_copy(config.password, password, sizeof(config.password));
        config.security_type = WIFI_SECURITY_WPA2;
    } else {
        config.password[0] = '\0';
        config.security_type = WIFI_SECURITY_NONE;
    }
    safe_string_copy(config.wifi_interface, wifi_interface, sizeof(config.wifi_interface));
    if (internet_interface) {
        safe_string_copy(config.internet_interface, internet_interface, sizeof(config.internet_interface));
        config.share_method = HOTSPOT_SHARE_NAT;
    } else {
        config.internet_interface[0] = '\0';  // Clear internet interface
        config.share_method = HOTSPOT_SHARE_NONE;
    }

    // Delete any existing configuration with the same name first
    hotspot_delete_config(config.name);

    // Try to create and start the hotspot
    result = hotspot_create_config(&config);
    if (result != WTERM_SUCCESS) {
        hotspot_manager_cleanup();
        return result;
    }

    result = hotspot_start(config.name, status);
    if (result != WTERM_SUCCESS) {
        hotspot_manager_cleanup();
        return result;
    }

    hotspot_manager_cleanup();
    return WTERM_SUCCESS;
}