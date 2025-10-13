/**
 * @file hotspot_manager.c
 * @brief WiFi hotspot management implementation for wterm
 */

#define _POSIX_C_SOURCE 200809L
#include "hotspot_manager.h"
#include "error_handler.h"
#include "error_queue.h"
#include "../utils/string_utils.h"
#include "../utils/safe_exec.h"
#include "../utils/input_sanitizer.h"
#include "../utils/iw_helper.h"
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
static wterm_result_t execute_nmcli_command_silent(const char *command, char *output, size_t output_size);
static void detect_gateway_ip(char *gateway_ip, size_t size);

// NAT management function declarations
static wterm_result_t get_default_route_interface(char *interface, size_t size);
static bool check_iptables_rule_exists(char **args);
static wterm_result_t setup_nat_rules(const char *hotspot_iface, const char *inet_iface, const char *hotspot_subnet);
static wterm_result_t cleanup_nat_rules(const char *hotspot_iface, const char *hotspot_subnet);

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

    // If 5GHz band is requested, verify interface supports it
    if (config->is_5ghz && iw_is_available()) {
        bool supports_5ghz = false;
        if (iw_check_5ghz_support(config->wifi_interface, &supports_5ghz) == WTERM_SUCCESS) {
            if (!supports_5ghz) {
                REPORT_ERROR(true, "Error: Interface %s does not support 5GHz band",
                        config->wifi_interface);
                return WTERM_ERROR_GENERAL;
            }
        } else {
            REPORT_ERROR(false, "Warning: Could not verify 5GHz support for interface %s",
                    config->wifi_interface);
        }
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

    char output[512];

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

    // Auto-detect gateway IP if not set (for old configs without gateway_ip)
    if (!config->gateway_ip[0]) {
        detect_gateway_ip(config->gateway_ip, sizeof(config->gateway_ip));

        // Save the updated config back to disk
        hotspot_save_config_to_file(config);
    }

    // Validate gateway IP is set (should always pass after auto-detection)
    if (!config->gateway_ip[0]) {
        REPORT_ERROR(true, "Hotspot configuration error: failed to detect gateway_ip");
        return WTERM_ERROR_INVALID_INPUT;
    }

    // Disconnect the WiFi interface if it's currently connected
    // (A WiFi interface can't be a client and AP at the same time - single radio limitation)
    char disconnect_cmd[512];
    snprintf(disconnect_cmd, sizeof(disconnect_cmd),
             "nmcli device disconnect %s 2>/dev/null", config->wifi_interface);
    execute_nmcli_command_silent(disconnect_cmd, output, sizeof(output));
    // Ignore errors - interface might not be connected

    // Check if NetworkManager connection already exists
    char check_command[256];
    snprintf(check_command, sizeof(check_command),
             "nmcli -t -f NAME connection show | grep -q '^%s$'", config->name);

    wterm_result_t check_result = execute_nmcli_command(check_command, output, sizeof(output));
    bool connection_exists = (check_result == WTERM_SUCCESS);

    if (connection_exists) {
        // Connection exists - update its settings to ensure correct configuration
        char modify_cmd[512];

        // Fix IPv4 method to "shared" using configured gateway IP
        snprintf(modify_cmd, sizeof(modify_cmd),
                 "nmcli connection modify %s ipv4.method shared ipv4.addresses %s/24 2>/dev/null",
                 config->name, config->gateway_ip);
        execute_nmcli_command(modify_cmd, output, sizeof(output)); // Ignore errors

        // Ensure band is configured (default to 2.4GHz if not set)
        snprintf(modify_cmd, sizeof(modify_cmd),
                 "nmcli connection modify %s 802-11-wireless.band bg 2>/dev/null",
                 config->name);
        execute_nmcli_command(modify_cmd, output, sizeof(output)); // Ignore errors
    } else {
        // Connection doesn't exist - create it
        char command[2048];
        int written;

        // Build IPv4 configuration string with configured gateway IP
        char ipv4_config[128];
        snprintf(ipv4_config, sizeof(ipv4_config),
                 "ipv4.method shared ipv4.addresses %s/24", config->gateway_ip);

        if (config->password[0] != '\0') {
            // Secured hotspot with password
            written = snprintf(command, sizeof(command),
                "nmcli connection add type wifi ifname %s con-name %s ssid %s "
                "802-11-wireless.mode ap "
                "802-11-wireless-security.key-mgmt wpa-psk "
                "802-11-wireless-security.psk \"%s\" "
                "connection.autoconnect no "
                "%s",
                config->wifi_interface, config->name, config->ssid, config->password, ipv4_config);
        } else {
            // Open hotspot without password
            written = snprintf(command, sizeof(command),
                "nmcli connection add type wifi ifname %s con-name %s ssid %s "
                "802-11-wireless.mode ap "
                "connection.autoconnect no "
                "%s",
                config->wifi_interface, config->name, config->ssid, ipv4_config);
        }

        if (written >= (int)sizeof(command)) {
            return WTERM_ERROR_GENERAL;
        }

        // Execute command to create connection
        wterm_result_t result = execute_nmcli_command(command, output, sizeof(output));
        if (result != WTERM_SUCCESS) {
            return result;
        }
    }

    // Start/activate the connection
    char command[256];
    snprintf(command, sizeof(command), "nmcli connection up %s", config->name);
    wterm_result_t result = execute_nmcli_command(command, output, sizeof(output));
    if (result != WTERM_SUCCESS) {
        return result;
    }

    // Configure NAT for internet sharing if sharing is enabled
    if (config->share_method == HOTSPOT_SHARE_NAT || config->share_method == HOTSPOT_SHARE_NONE) {
        // Try to detect internet interface
        char inet_iface[MAX_STR_INTERFACE] = {0};
        if (get_default_route_interface(inet_iface, sizeof(inet_iface)) == WTERM_SUCCESS) {
            // Get hotspot subnet (use gateway_ip from config)
            char subnet[64];
            snprintf(subnet, sizeof(subnet), "%s", config->gateway_ip);

            // Set up NAT rules (silently)
            setup_nat_rules(config->wifi_interface, inet_iface, subnet);
        }
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

    // If stopping a specific hotspot, clean up NAT rules first
    if (name) {
        // Find configuration to get interface and subnet info
        hotspot_config_t *config = NULL;
        for (int i = 0; i < saved_configs.count; i++) {
            if (strcmp(saved_configs.hotspots[i].name, name) == 0) {
                config = &saved_configs.hotspots[i];
                break;
            }
        }

        // Clean up NAT rules before stopping hotspot (silently)
        if (config) {
            cleanup_nat_rules(config->wifi_interface, config->gateway_ip);
        }
    }

    char command[256];
    if (name) {
        // Stop specific hotspot (silently - ignore errors if already stopped)
        snprintf(command, sizeof(command), "nmcli connection down %s 2>/dev/null", name);
    } else {
        // Stop all active wifi connections in AP mode
        snprintf(command, sizeof(command),
                "nmcli -t -f NAME,TYPE connection show --active | grep wifi | cut -d: -f1 | xargs -I {} nmcli connection down {} 2>/dev/null");
    }

    char output[512];
    return execute_nmcli_command_silent(command, output, sizeof(output));
}

wterm_result_t hotspot_get_status(const char *name, hotspot_status_t *status) {
    if (!manager_initialized || !name || !status) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    // Find configuration in saved configs (wterm-created)
    hotspot_config_t *config = NULL;
    for (int i = 0; i < saved_configs.count; i++) {
        if (strcmp(saved_configs.hotspots[i].name, name) == 0) {
            config = &saved_configs.hotspots[i];
            break;
        }
    }

    // If not found in saved configs, check if it's an external hotspot
    hotspot_config_t external_config;
    if (!config) {
        // Verify this is actually a hotspot in NetworkManager
        char check_cmd[512];
        snprintf(check_cmd, sizeof(check_cmd),
                 "nmcli -t -f 802-11-wireless.mode connection show '%s' 2>/dev/null",
                 name);

        FILE *mode_fp = popen(check_cmd, "r");
        if (!mode_fp) {
            return WTERM_ERROR_GENERAL;
        }

        char mode[64] = {0};
        bool is_hotspot = false;
        if (fgets(mode, sizeof(mode), mode_fp)) {
            mode[strcspn(mode, "\n")] = '\0';
            char *mode_value = strchr(mode, ':');
            if (mode_value) {
                mode_value++; // Skip ':'
                if (strcmp(mode_value, "ap") == 0) {
                    is_hotspot = true;
                }
            }
        }
        pclose(mode_fp);

        if (!is_hotspot) {
            return WTERM_ERROR_GENERAL; // Not a hotspot
        }

        // Create minimal config for external hotspot
        memset(&external_config, 0, sizeof(hotspot_config_t));
        safe_string_copy(external_config.name, name, sizeof(external_config.name));
        safe_string_copy(external_config.ssid, name, sizeof(external_config.ssid));
        config = &external_config;
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

    list->count = 0;

    // First, add wterm-created hotspots from saved configs
    for (int i = 0; i < saved_configs.count && list->count < MAX_HOTSPOTS; i++) {
        memcpy(&list->hotspots[list->count], &saved_configs.hotspots[i],
               sizeof(hotspot_config_t));
        list->count++;
    }

    // Then, scan NetworkManager for ALL hotspot connections (including external ones)
    FILE *fp = popen("nmcli -t -f NAME,TYPE connection show 2>/dev/null", "r");
    if (!fp) {
        // Return what we have from saved configs
        return WTERM_SUCCESS;
    }

    char line[512];
    while (fgets(line, sizeof(line), fp) && list->count < MAX_HOTSPOTS) {
        line[strcspn(line, "\n")] = '\0';

        // Parse: NAME:TYPE
        char *name_field = strtok(line, ":");
        char *type_field = strtok(NULL, ":");

        if (!name_field || !type_field) continue;
        if (strcmp(type_field, "802-11-wireless") != 0) continue;

        // Check if already in list (from wterm configs)
        bool already_listed = false;
        for (int i = 0; i < list->count; i++) {
            if (strcmp(list->hotspots[i].name, name_field) == 0) {
                already_listed = true;
                break;
            }
        }
        if (already_listed) continue;

        // Check if this WiFi connection is actually a hotspot (mode = ap)
        char check_cmd[512];
        snprintf(check_cmd, sizeof(check_cmd),
                 "nmcli -t -f 802-11-wireless.mode connection show '%s' 2>/dev/null",
                 name_field);

        FILE *mode_fp = popen(check_cmd, "r");
        if (!mode_fp) continue;

        char mode[64] = {0};
        if (fgets(mode, sizeof(mode), mode_fp)) {
            mode[strcspn(mode, "\n")] = '\0';

            // Extract mode value (format: "802-11-wireless.mode:ap")
            char *mode_value = strchr(mode, ':');
            if (mode_value) {
                mode_value++; // Skip ':'

                if (strcmp(mode_value, "ap") == 0) {
                    // This is a hotspot! Add it to the list
                    hotspot_config_t *hs = &list->hotspots[list->count];
                    memset(hs, 0, sizeof(hotspot_config_t));

                    safe_string_copy(hs->name, name_field, sizeof(hs->name));
                    safe_string_copy(hs->ssid, name_field, sizeof(hs->ssid)); // Use name as SSID
                    // Mark as external by leaving other fields empty

                    list->count++;
                }
            }
        }
        pclose(mode_fp);
    }
    pclose(fp);

    return WTERM_SUCCESS;
}

wterm_result_t hotspot_delete_config(const char *name) {
    if (!manager_initialized || !name) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    // Try to stop hotspot if running (silently - ignore errors if already stopped)
    char stop_cmd[256];
    snprintf(stop_cmd, sizeof(stop_cmd), "nmcli connection down %s 2>/dev/null", name);
    FILE *fp = popen(stop_cmd, "r");
    if (fp) {
        pclose(fp); // Ignore return value - hotspot might already be stopped
    }

    // Check if this is a wterm-managed hotspot or external
    int found_index = -1;
    for (int i = 0; i < saved_configs.count; i++) {
        if (strcmp(saved_configs.hotspots[i].name, name) == 0) {
            found_index = i;
            break;
        }
    }

    // If it's a wterm-managed hotspot, remove from saved configs
    bool was_wterm_managed = false;
    if (found_index != -1) {
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
        was_wterm_managed = true;
    }

    // Delete NetworkManager connection (works for both wterm and external hotspots)
    // This is optional cleanup - ignore errors if connection doesn't exist
    char command[256];
    snprintf(command, sizeof(command), "nmcli connection delete %s 2>/dev/null", name);
    char output[512];
    execute_nmcli_command_silent(command, output, sizeof(output));

    // Return success if we successfully deleted the wterm config
    // NetworkManager connection deletion is optional cleanup
    return was_wterm_managed ? WTERM_SUCCESS : WTERM_ERROR_GENERAL;
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

/**
 * @brief Auto-detect a non-conflicting gateway IP based on current network
 */
static void detect_gateway_ip(char *gateway_ip, size_t size) {
    if (!gateway_ip || size == 0) {
        return;
    }

    // Get current IP address
    FILE *fp = popen("ip -4 addr show scope global 2>/dev/null | grep -oP '(?<=inet\\s)\\d+(\\.\\d+){3}' | head -1", "r");
    if (!fp) {
        // Fallback to default
        safe_string_copy(gateway_ip, "192.168.12.1", size);
        return;
    }

    char current_ip[32] = {0};
    if (fgets(current_ip, sizeof(current_ip), fp)) {
        current_ip[strcspn(current_ip, "\n")] = '\0';

        // Parse first three octets to detect pattern
        int octet1 = 0, octet2 = 0, octet3 = 0;
        if (sscanf(current_ip, "%d.%d.%d", &octet1, &octet2, &octet3) >= 2) {
            // Choose non-conflicting third octet based on network class
            int new_third_octet;
            if (octet1 == 10) {
                // Class A private: use 42 if current is not 42
                new_third_octet = (octet2 == 42) ? 43 : 42;
            } else if (octet1 == 172 && octet2 >= 16 && octet2 <= 31) {
                // Class B private: keep second octet, change third
                new_third_octet = (octet3 == 12) ? 13 : 12;
            } else if (octet1 == 192 && octet2 == 168) {
                // Class C private: keep 192.168, change third octet
                new_third_octet = (octet3 == 12) ? 13 : 12;
            } else {
                // Other networks: use 12
                new_third_octet = (octet3 == 12) ? 13 : 12;
            }
            snprintf(gateway_ip, size, "%d.%d.%d.1", octet1, octet2, new_third_octet);
        } else {
            // Failed to parse, use default
            safe_string_copy(gateway_ip, "192.168.12.1", size);
        }
    } else {
        // No IP found, use default
        safe_string_copy(gateway_ip, "192.168.12.1", size);
    }

    pclose(fp);
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

    // Auto-detect gateway IP based on current network
    detect_gateway_ip(config->gateway_ip, sizeof(config->gateway_ip));

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
    saved_configs.count = 0;

    // Use shell command to list .conf files
    char command[512];
    snprintf(command, sizeof(command), "ls %s/*.conf 2>/dev/null", HOTSPOT_CONFIG_DIR);

    FILE *fp = popen(command, "r");
    if (!fp) {
        return WTERM_SUCCESS; // No configs found
    }

    char filepath[512];
    while (fgets(filepath, sizeof(filepath), fp) && saved_configs.count < MAX_HOTSPOTS) {
        filepath[strcspn(filepath, "\n")] = '\0';

        // Read config file
        FILE *conf_fp = fopen(filepath, "r");
        if (!conf_fp) continue;

        hotspot_config_t *cfg = &saved_configs.hotspots[saved_configs.count];
        memset(cfg, 0, sizeof(hotspot_config_t));

        char line[512];
        while (fgets(line, sizeof(line), conf_fp)) {
            line[strcspn(line, "\n")] = '\0';

            char *equals = strchr(line, '=');
            if (!equals) continue;

            *equals = '\0';
            const char *key = line;
            const char *value = equals + 1;

            if (strcmp(key, "name") == 0) safe_string_copy(cfg->name, value, sizeof(cfg->name));
            else if (strcmp(key, "ssid") == 0) safe_string_copy(cfg->ssid, value, sizeof(cfg->ssid));
            else if (strcmp(key, "password") == 0) safe_string_copy(cfg->password, value, sizeof(cfg->password));
            else if (strcmp(key, "wifi_interface") == 0) safe_string_copy(cfg->wifi_interface, value, sizeof(cfg->wifi_interface));
            else if (strcmp(key, "internet_interface") == 0) safe_string_copy(cfg->internet_interface, value, sizeof(cfg->internet_interface));
            else if (strcmp(key, "gateway_ip") == 0) safe_string_copy(cfg->gateway_ip, value, sizeof(cfg->gateway_ip));
            else if (strcmp(key, "security_type") == 0) cfg->security_type = atoi(value);
            else if (strcmp(key, "share_method") == 0) cfg->share_method = atoi(value);
            else if (strcmp(key, "channel") == 0) cfg->channel = atoi(value);
            else if (strcmp(key, "hidden") == 0) cfg->hidden = (atoi(value) != 0);
            else if (strcmp(key, "client_isolation") == 0) cfg->client_isolation = (atoi(value) != 0);
            else if (strcmp(key, "mac_filtering") == 0) cfg->mac_filtering = (atoi(value) != 0);
            else if (strcmp(key, "is_5ghz") == 0) cfg->is_5ghz = (atoi(value) != 0);
        }

        fclose(conf_fp);

        // Only add if name is valid
        if (cfg->name[0] != '\0') {
            saved_configs.count++;
        }
    }

    pclose(fp);
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
        return WTERM_ERROR_NETWORK;
    }

    return WTERM_SUCCESS;
}

// Silent version - doesn't print errors
static wterm_result_t execute_nmcli_command_silent(const char *command, char *output, size_t output_size) {
    return execute_nmcli_command(command, output, output_size);
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

// NAT management function implementations

/**
 * @brief Get the default route interface (internet source)
 */
static wterm_result_t get_default_route_interface(char *interface, size_t size) {
    if (!interface || size == 0) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    // Use popen to capture output from: ip route show default
    // Note: This is safe as we're not passing user input to the command
    FILE *fp = popen("ip route show default 2>/dev/null", "r");
    if (!fp) {
        return WTERM_ERROR_NETWORK;
    }

    char line[256];
    bool found = false;

    if (fgets(line, sizeof(line), fp)) {
        // Parse: "default via 10.246.251.85 dev enp0s20f0u5 proto dhcp..."
        char *dev_pos = strstr(line, " dev ");
        if (dev_pos) {
            dev_pos += 5; // Skip " dev "
            char *space = strchr(dev_pos, ' ');
            size_t len = space ? (size_t)(space - dev_pos) : strlen(dev_pos);

            if (len < size) {
                strncpy(interface, dev_pos, len);
                interface[len] = '\0';
                // Remove newline if present
                interface[strcspn(interface, "\n")] = '\0';
                found = true;
            }
        }
    }

    pclose(fp);
    return found ? WTERM_SUCCESS : WTERM_ERROR_NETWORK;
}

/**
 * @brief Check if an iptables rule already exists
 */
static bool check_iptables_rule_exists(char **args) {
    if (!args) {
        return false;
    }

    // Convert -A to -C (check mode) for iptables
    // Build check args by replacing -A with -C
    int arg_count = 0;
    while (args[arg_count] != NULL) {
        arg_count++;
    }

    char **check_args = malloc(sizeof(char*) * (arg_count + 1));
    if (!check_args) {
        return false;
    }

    for (int i = 0; i < arg_count; i++) {
        if (strcmp(args[i], "-A") == 0) {
            check_args[i] = "-C"; // Check mode instead of append
        } else {
            check_args[i] = args[i];
        }
    }
    check_args[arg_count] = NULL;

    // Execute check command (returns 0 if rule exists)
    int result = safe_exec_command("iptables", check_args);
    free(check_args);

    return (result == 0);
}

/**
 * @brief Set up NAT rules for internet sharing (with duplicate prevention)
 */
static wterm_result_t setup_nat_rules(const char *hotspot_iface, const char *inet_iface,
                                      const char *hotspot_subnet) {
    if (!hotspot_iface || !inet_iface || !hotspot_subnet) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    // Check if running as root - NAT requires root privileges
    // Silently skip NAT setup if not root (hotspot will still work, just no internet sharing)
    if (geteuid() != 0) {
        return WTERM_SUCCESS;
    }

    // Validate interface names
    if (!validate_interface_name(hotspot_iface) || !validate_interface_name(inet_iface)) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    // Extract subnet (e.g., "192.168.12.1/24" -> "192.168.12.0/24")
    char subnet[32];
    safe_string_copy(subnet, hotspot_subnet, sizeof(subnet));

    // Create NAT MASQUERADE rule (check if exists first)
    char *masq_args[] = {
        "iptables", "-t", "nat", "-A", "POSTROUTING",
        "-s", subnet, "!", "-d", subnet,
        "-j", "MASQUERADE",
        NULL
    };

    if (!check_iptables_rule_exists(masq_args)) {
        if (safe_exec_command("iptables", masq_args) != 0) {
            return WTERM_ERROR_NETWORK;
        }
    }

    // Add FORWARD rule (hotspot -> internet)
    char *forward_out_args[] = {
        "iptables", "-A", "FORWARD",
        "-i", (char*)hotspot_iface, "-o", (char*)inet_iface,
        "-j", "ACCEPT",
        NULL
    };

    if (!check_iptables_rule_exists(forward_out_args)) {
        if (safe_exec_command("iptables", forward_out_args) != 0) {
            return WTERM_ERROR_NETWORK;
        }
    }

    // Add FORWARD rule (internet -> hotspot, established connections)
    char *forward_in_args[] = {
        "iptables", "-A", "FORWARD",
        "-i", (char*)inet_iface, "-o", (char*)hotspot_iface,
        "-m", "state", "--state", "RELATED,ESTABLISHED",
        "-j", "ACCEPT",
        NULL
    };

    if (!check_iptables_rule_exists(forward_in_args)) {
        safe_exec_command("iptables", forward_in_args);  // Non-critical, ignore errors
    }

    return WTERM_SUCCESS;
}

/**
 * @brief Clean up NAT rules when stopping hotspot
 */
static wterm_result_t cleanup_nat_rules(const char *hotspot_iface, const char *hotspot_subnet) {
    if (!hotspot_iface || !hotspot_subnet) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    // Check if running as root - NAT cleanup requires root privileges
    // Silently skip if not root
    if (geteuid() != 0) {
        return WTERM_SUCCESS;
    }

    // Extract subnet
    char subnet[32];
    safe_string_copy(subnet, hotspot_subnet, sizeof(subnet));

    // Remove NAT MASQUERADE rule
    char *del_masq_args[] = {
        "iptables", "-t", "nat", "-D", "POSTROUTING",
        "-s", subnet, "!", "-d", subnet,
        "-j", "MASQUERADE",
        NULL
    };
    safe_exec_command("iptables", del_masq_args); // Ignore errors

    // Remove FORWARD rules - try to remove all matching rules
    // We use a loop because there might be multiple rules
    for (int i = 0; i < 10; i++) {  // Max 10 attempts
        char *del_forward_args[] = {
            "iptables", "-D", "FORWARD",
            "-i", (char*)hotspot_iface,
            "-j", "ACCEPT",
            NULL
        };

        if (safe_exec_command("iptables", del_forward_args) != 0) {
            break; // No more rules to delete
        }
    }

    // Remove inbound FORWARD rules
    for (int i = 0; i < 10; i++) {
        char *del_forward_in_args[] = {
            "iptables", "-D", "FORWARD",
            "-o", (char*)hotspot_iface,
            "-m", "state", "--state", "RELATED,ESTABLISHED",
            "-j", "ACCEPT",
            NULL
        };

        if (safe_exec_command("iptables", del_forward_in_args) != 0) {
            break;
        }
    }

    return WTERM_SUCCESS;
}

// UI Helper Functions

/**
 * @brief Get list of available WiFi interfaces
 */
wterm_result_t hotspot_get_interface_list(interface_info_t *interfaces,
                                          int max_count, int *count) {
    if (!interfaces || !count || max_count <= 0) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    *count = 0;

    // Use nmcli to list WiFi interfaces
    FILE *fp = popen("nmcli device status 2>/dev/null", "r");
    if (!fp) {
        return WTERM_ERROR_NETWORK;
    }

    char line[256];
    // Skip header line
    if (fgets(line, sizeof(line), fp) == NULL) {
        pclose(fp);
        return WTERM_ERROR_NETWORK;
    }

    // Parse each interface line
    while (fgets(line, sizeof(line), fp) && *count < max_count) {
        char device[MAX_STR_INTERFACE];
        char type[32];
        char state[32];

        // Parse: DEVICE TYPE STATE CONNECTION
        if (sscanf(line, "%15s %31s %31s", device, type, state) == 3) {
            // Only include WiFi interfaces
            if (strcmp(type, "wifi") == 0) {
                safe_string_copy(interfaces[*count].name, device, sizeof(interfaces[*count].name));
                safe_string_copy(interfaces[*count].status, state, sizeof(interfaces[*count].status));

                // Use iw to verify AP mode support
                bool supports_ap = false;
                if (iw_is_available() && iw_check_ap_mode_support(device, &supports_ap) == WTERM_SUCCESS) {
                    interfaces[*count].supports_ap = supports_ap;
                } else {
                    // Fallback: assume WiFi interfaces support AP mode
                    interfaces[*count].supports_ap = true;
                }

                (*count)++;
            }
        }
    }

    pclose(fp);
    return (*count > 0) ? WTERM_SUCCESS : WTERM_ERROR_NETWORK;
}

/**
 * @brief Get available frequency band options
 */
wterm_result_t hotspot_get_band_options(band_option_t *bands, int *count) {
    if (!bands || !count) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    // Static band options
    safe_string_copy(bands[0].id, "bg", sizeof(bands[0].id));
    safe_string_copy(bands[0].display, "2.4GHz (Better range, more compatible)",
                    sizeof(bands[0].display));

    safe_string_copy(bands[1].id, "a", sizeof(bands[1].id));
    safe_string_copy(bands[1].display, "5GHz (Faster, less interference)",
                    sizeof(bands[1].display));

    *count = 2;
    return WTERM_SUCCESS;
}

/**
 * @brief Get available security options
 */
wterm_result_t hotspot_get_security_options(security_option_t *options, int *count) {
    if (!options || !count) {
        return WTERM_ERROR_INVALID_INPUT;
    }

    // Static security options
    safe_string_copy(options[0].id, "secured", sizeof(options[0].id));
    safe_string_copy(options[0].display, "Secured (WPA2-PSK)",
                    sizeof(options[0].display));

    safe_string_copy(options[1].id, "open", sizeof(options[1].id));
    safe_string_copy(options[1].display, "Open (No password)",
                    sizeof(options[1].display));

    *count = 2;
    return WTERM_SUCCESS;
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