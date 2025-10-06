/**
 * @file nmcli_backend.c
 * @brief NetworkManager (nmcli) backend implementation
 */

#define _POSIX_C_SOURCE 200809L
#include "../../utils/input_sanitizer.h"
#include "../../utils/safe_exec.h"
#include "../../utils/string_utils.h"
#include "../network_scanner.h"
#include "backend_interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// nmcli command definitions
#define NMCLI_WIFI_LIST "nmcli -t -f SSID,SECURITY,SIGNAL device wifi list"
#define NMCLI_WIFI_SCAN "nmcli device wifi rescan"
#define NMCLI_WIFI_CONNECT "nmcli device wifi connect"

// Forward declarations
static wterm_result_t nmcli_scan_networks(network_list_t *networks);
static backend_result_t nmcli_connect_open_network(const char *ssid);
static backend_result_t nmcli_connect_secured_network(const char *ssid,
                                                      const char *password);
static backend_result_t nmcli_disconnect_network(void);
static wterm_result_t nmcli_rescan_networks(void);
static bool nmcli_is_available(void);
static bool nmcli_is_connected(char *connected_ssid, size_t buffer_size);
static bool nmcli_get_ip_address(char *ip_buffer, size_t buffer_size);

// Hotspot operation declarations
static backend_result_t nmcli_create_hotspot(const hotspot_config_t *config);
static backend_result_t nmcli_start_hotspot(const char *hotspot_name);
static backend_result_t nmcli_stop_hotspot(const char *hotspot_name);
static backend_result_t nmcli_delete_hotspot(const char *hotspot_name);
static wterm_result_t nmcli_get_hotspot_status(const char *hotspot_name,
                                               hotspot_status_t *status);
static wterm_result_t
nmcli_list_active_hotspots(char active_hotspots[][MAX_STR_SSID], int max_count,
                           int *count);
static wterm_result_t nmcli_get_hotspot_clients(const char *hotspot_name,
                                                hotspot_client_t *clients,
                                                int max_clients,
                                                int *client_count);
static wterm_result_t nmcli_check_interface_ap_support(const char *interface,
                                                       bool *supports_ap);
static wterm_result_t
nmcli_get_available_wifi_interfaces(char interfaces[][MAX_STR_INTERFACE],
                                    int max_interfaces, int *interface_count);

// Backend definition
const network_backend_t nmcli_backend = {
    .type = NETMGR_NMCLI,
    .name = "NetworkManager",
    .command = "nmcli",
    .scan_networks = nmcli_scan_networks,
    .connect_open_network = nmcli_connect_open_network,
    .connect_secured_network = nmcli_connect_secured_network,
    .disconnect_network = nmcli_disconnect_network,
    .rescan_networks = nmcli_rescan_networks,
    .is_available = nmcli_is_available,
    .is_connected = nmcli_is_connected,
    .get_ip_address = nmcli_get_ip_address,
    .create_hotspot = nmcli_create_hotspot,
    .start_hotspot = nmcli_start_hotspot,
    .stop_hotspot = nmcli_stop_hotspot,
    .delete_hotspot = nmcli_delete_hotspot,
    .get_hotspot_status = nmcli_get_hotspot_status,
    .list_active_hotspots = nmcli_list_active_hotspots,
    .get_hotspot_clients = nmcli_get_hotspot_clients,
    .check_interface_ap_support = nmcli_check_interface_ap_support,
    .get_available_wifi_interfaces = nmcli_get_available_wifi_interfaces};

static bool nmcli_is_available(void) { return safe_command_exists("nmcli"); }

// Use the shared parsing function from network_scanner.h

static wterm_result_t nmcli_scan_networks(network_list_t *networks) {
  if (!networks) {
    return WTERM_ERROR_INVALID_INPUT;
  }

  networks->count = 0;

  FILE *fp = popen(NMCLI_WIFI_LIST, "r");
  if (!fp) {
    return WTERM_ERROR_NETWORK;
  }

  char buffer[512];
  while (fgets(buffer, sizeof(buffer), fp) && networks->count < MAX_NETWORKS) {
    // Remove trailing newline
    buffer[strcspn(buffer, "\n")] = '\0';

    if (strlen(buffer) == 0)
      continue; // Skip empty lines

    network_info_t *network = &networks->networks[networks->count];
    if (parse_network_line(buffer, network) == WTERM_SUCCESS) {
      networks->count++;
    }
  }

  int exit_code = pclose(fp);
  if (exit_code != 0) {
    return WTERM_ERROR_NETWORK;
  }

  return WTERM_SUCCESS;
}

// Helper function to execute nmcli connection and handle errors
static backend_result_t execute_nmcli_command(const char *command) {
  backend_result_t result = {.result = WTERM_SUCCESS};

  FILE *fp = popen(command, "r");
  if (!fp) {
    result.result = WTERM_ERROR_NETWORK;
    safe_string_copy(result.error_message, "Failed to execute nmcli command",
                     sizeof(result.error_message));
    return result;
  }

  char error_output[256] = {0};
  if (fgets(error_output, sizeof(error_output), fp)) {
    trim_trailing_whitespace(error_output);
  }

  int exit_code = pclose(fp);
  if (exit_code != 0) {
    result.result = WTERM_ERROR_NETWORK;
    if (strlen(error_output) > 0) {
      safe_string_copy(result.error_message, error_output,
                       sizeof(result.error_message));
    } else {
      safe_string_copy(result.error_message, "Command failed",
                       sizeof(result.error_message));
    }
  }

  return result;
}

static backend_result_t nmcli_connect_open_network(const char *ssid) {
  backend_result_t result = {.result = WTERM_SUCCESS};

  if (!ssid) {
    result.result = WTERM_ERROR_INVALID_INPUT;
    safe_string_copy(result.error_message, "Invalid SSID",
                     sizeof(result.error_message));
    return result;
  }

  // Validate SSID
  if (!validate_ssid(ssid)) {
    result.result = WTERM_ERROR_INVALID_INPUT;
    safe_string_copy(result.error_message,
                     "SSID contains invalid characters or length",
                     sizeof(result.error_message));
    return result;
  }

  // Escape SSID for shell safety
  char escaped_ssid[256];
  if (!shell_escape(ssid, escaped_ssid, sizeof(escaped_ssid))) {
    result.result = WTERM_ERROR_INVALID_INPUT;
    safe_string_copy(result.error_message, "SSID too long for shell escaping",
                     sizeof(result.error_message));
    return result;
  }

  char command[512];
  snprintf(command, sizeof(command), "%s %s 2>&1", NMCLI_WIFI_CONNECT,
           escaped_ssid);

  return execute_nmcli_command(command);
}

static backend_result_t nmcli_connect_secured_network(const char *ssid,
                                                      const char *password) {
  backend_result_t result = {.result = WTERM_SUCCESS};

  if (!ssid || !password) {
    result.result = WTERM_ERROR_INVALID_INPUT;
    safe_string_copy(result.error_message, "Invalid SSID or password",
                     sizeof(result.error_message));
    return result;
  }

  // Validate SSID
  if (!validate_ssid(ssid)) {
    result.result = WTERM_ERROR_INVALID_INPUT;
    safe_string_copy(result.error_message,
                     "SSID contains invalid characters or length",
                     sizeof(result.error_message));
    return result;
  }

  // Escape both SSID and password for shell safety
  char escaped_ssid[256];
  char escaped_password[512];

  if (!shell_escape(ssid, escaped_ssid, sizeof(escaped_ssid))) {
    result.result = WTERM_ERROR_INVALID_INPUT;
    safe_string_copy(result.error_message, "SSID too long for shell escaping",
                     sizeof(result.error_message));
    return result;
  }

  if (!shell_escape(password, escaped_password, sizeof(escaped_password))) {
    result.result = WTERM_ERROR_INVALID_INPUT;
    safe_string_copy(result.error_message,
                     "Password too long for shell escaping",
                     sizeof(result.error_message));
    return result;
  }

  char command[1024];
  snprintf(command, sizeof(command), "%s %s password %s 2>&1",
           NMCLI_WIFI_CONNECT, escaped_ssid, escaped_password);

  return execute_nmcli_command(command);
}

static backend_result_t nmcli_disconnect_network(void) {
  return execute_nmcli_command("nmcli device wifi disconnect 2>&1");
}

static wterm_result_t nmcli_rescan_networks(void) {
  char *const args[] = {"nmcli", "device", "wifi", "rescan", NULL};
  bool result = safe_exec_check("nmcli", args);
  return result ? WTERM_SUCCESS : WTERM_ERROR_NETWORK;
}

static bool nmcli_is_connected(char *connected_ssid, size_t buffer_size) {
  if (!connected_ssid || buffer_size == 0) {
    return false;
  }

  FILE *fp = popen("nmcli -t -f ACTIVE,SSID device wifi list", "r");
  if (!fp) {
    return false;
  }

  char buffer[256];
  bool found_connection = false;

  while (fgets(buffer, sizeof(buffer), fp)) {
    buffer[strcspn(buffer, "\n")] = '\0';

    if (strncmp(buffer, "yes:", 4) == 0) {
      const char *ssid = buffer + 4; // Skip "yes:"
      safe_string_copy(connected_ssid, ssid, buffer_size);
      found_connection = true;
      break;
    }
  }

  pclose(fp);
  return found_connection;
}

static bool nmcli_get_ip_address(char *ip_buffer, size_t buffer_size) {
  if (!ip_buffer || buffer_size == 0) {
    return false;
  }

  FILE *fp = popen(
      "nmcli -t -f IP4.ADDRESS connection show --active 2>/dev/null | head -1",
      "r");
  if (!fp) {
    return false;
  }

  char buffer[64];
  bool found_ip = false;

  if (fgets(buffer, sizeof(buffer), fp)) {
    buffer[strcspn(buffer, "\n")] = '\0';

    // Remove "/XX" subnet notation if present
    char *slash = strchr(buffer, '/');
    if (slash)
      *slash = '\0';

    if (strlen(buffer) > 0) {
      safe_string_copy(ip_buffer, buffer, buffer_size);
      found_ip = true;
    }
  }

  pclose(fp);
  return found_ip;
}

// Hotspot operation implementations
static backend_result_t nmcli_create_hotspot(const hotspot_config_t *config) {
  backend_result_t result = {.result = WTERM_SUCCESS};

  if (!config) {
    result.result = WTERM_ERROR_INVALID_INPUT;
    safe_string_copy(result.error_message, "Invalid hotspot configuration",
                     sizeof(result.error_message));
    return result;
  }

  // Validate all inputs
  if (!validate_interface_name(config->wifi_interface)) {
    result.result = WTERM_ERROR_INVALID_INPUT;
    safe_string_copy(result.error_message, "Invalid WiFi interface name",
                     sizeof(result.error_message));
    return result;
  }

  if (!validate_hotspot_name(config->name)) {
    result.result = WTERM_ERROR_INVALID_INPUT;
    safe_string_copy(result.error_message, "Invalid hotspot name",
                     sizeof(result.error_message));
    return result;
  }

  if (!validate_ssid(config->ssid)) {
    result.result = WTERM_ERROR_INVALID_INPUT;
    safe_string_copy(result.error_message, "Invalid SSID",
                     sizeof(result.error_message));
    return result;
  }

  // Escape all user-controlled parameters
  char escaped_ssid[256];

  if (!shell_escape(config->ssid, escaped_ssid, sizeof(escaped_ssid))) {
    result.result = WTERM_ERROR_INVALID_INPUT;
    safe_string_copy(result.error_message, "SSID too long for shell escaping",
                     sizeof(result.error_message));
    return result;
  }

  // Build complete nmcli command for creating WiFi AP connection
  char command[2048];
  int written;

  if (config->password[0] != '\0' &&
      config->security_type != WIFI_SECURITY_NONE) {
    // Secured hotspot with password
    char escaped_password[512];
    if (!shell_escape(config->password, escaped_password,
                      sizeof(escaped_password))) {
      result.result = WTERM_ERROR_INVALID_INPUT;
      safe_string_copy(result.error_message,
                       "Password too long for shell escaping",
                       sizeof(result.error_message));
      return result;
    }

    written = snprintf(
        command, sizeof(command),
        "nmcli connection add type wifi ifname %s con-name %s ssid %s "
        "802-11-wireless.mode ap "
        "802-11-wireless-security.key-mgmt wpa-psk "
        "802-11-wireless-security.psk %s "
        "ipv4.method shared "
        "ipv4.addresses 192.168.12.1/24",
        config->wifi_interface, config->name, escaped_ssid, escaped_password);
  } else {
    // Open hotspot without password
    written =
        snprintf(command, sizeof(command),
                 "nmcli connection add type wifi ifname %s con-name %s ssid %s "
                 "802-11-wireless.mode ap "
                 "ipv4.method shared "
                 "ipv4.addresses 192.168.12.1/24",
                 config->wifi_interface, config->name, escaped_ssid);
  }

  if (written >= (int)sizeof(command)) {
    result.result = WTERM_ERROR_GENERAL;
    safe_string_copy(result.error_message, "Command too long",
                     sizeof(result.error_message));
    return result;
  }

  // Add optional channel configuration
  if (config->channel > 0) {
    char temp[2048];
    written = snprintf(temp, sizeof(temp), "%s 802-11-wireless.channel %d",
                       command, config->channel);
    if (written >= (int)sizeof(temp)) {
      result.result = WTERM_ERROR_GENERAL;
      safe_string_copy(result.error_message, "Command with channel too long",
                       sizeof(result.error_message));
      return result;
    }
    safe_string_copy(command, temp, sizeof(command));
  }

  // Add optional band configuration (2.4GHz or 5GHz)
  if (config->is_5ghz) {
    char temp[2048];
    written =
        snprintf(temp, sizeof(temp), "%s 802-11-wireless.band a", command);
    if (written >= (int)sizeof(temp)) {
      result.result = WTERM_ERROR_GENERAL;
      safe_string_copy(result.error_message, "Command with band too long",
                       sizeof(result.error_message));
      return result;
    }
    safe_string_copy(command, temp, sizeof(command));
  } else {
    char temp[2048];
    written =
        snprintf(temp, sizeof(temp), "%s 802-11-wireless.band bg", command);
    if (written >= (int)sizeof(temp)) {
      result.result = WTERM_ERROR_GENERAL;
      safe_string_copy(result.error_message, "Command with band too long",
                       sizeof(result.error_message));
      return result;
    }
    safe_string_copy(command, temp, sizeof(command));
  }

  // Execute command
  char command_with_stderr[2056]; // Increased buffer size
  written = snprintf(command_with_stderr, sizeof(command_with_stderr),
                     "%s 2>&1", command);
  if (written >= (int)sizeof(command_with_stderr)) {
    result.result = WTERM_ERROR_GENERAL;
    safe_string_copy(result.error_message, "Final command too long",
                     sizeof(result.error_message));
    return result;
  }

  return execute_nmcli_command(command_with_stderr);
}

static backend_result_t nmcli_start_hotspot(const char *hotspot_name) {
  backend_result_t result = {.result = WTERM_SUCCESS};

  if (!hotspot_name) {
    result.result = WTERM_ERROR_INVALID_INPUT;
    safe_string_copy(result.error_message, "Invalid hotspot name",
                     sizeof(result.error_message));
    return result;
  }

  // Validate hotspot name
  if (!validate_hotspot_name(hotspot_name)) {
    result.result = WTERM_ERROR_INVALID_INPUT;
    safe_string_copy(result.error_message,
                     "Hotspot name contains invalid characters",
                     sizeof(result.error_message));
    return result;
  }

  char command[256];
  // hotspot_name is already validated, safe to use directly
  snprintf(command, sizeof(command), "nmcli connection up %s 2>&1",
           hotspot_name);

  return execute_nmcli_command(command);
}

static backend_result_t nmcli_stop_hotspot(const char *hotspot_name) {
  backend_result_t result = {.result = WTERM_SUCCESS};

  if (!hotspot_name) {
    result.result = WTERM_ERROR_INVALID_INPUT;
    safe_string_copy(result.error_message, "Invalid hotspot name",
                     sizeof(result.error_message));
    return result;
  }

  // Validate hotspot name
  if (!validate_hotspot_name(hotspot_name)) {
    result.result = WTERM_ERROR_INVALID_INPUT;
    safe_string_copy(result.error_message,
                     "Hotspot name contains invalid characters",
                     sizeof(result.error_message));
    return result;
  }

  char command[256];
  snprintf(command, sizeof(command), "nmcli connection down %s 2>&1",
           hotspot_name);

  return execute_nmcli_command(command);
}

static backend_result_t nmcli_delete_hotspot(const char *hotspot_name) {
  backend_result_t result = {.result = WTERM_SUCCESS};

  if (!hotspot_name) {
    result.result = WTERM_ERROR_INVALID_INPUT;
    safe_string_copy(result.error_message, "Invalid hotspot name",
                     sizeof(result.error_message));
    return result;
  }

  // Validate hotspot name
  if (!validate_hotspot_name(hotspot_name)) {
    result.result = WTERM_ERROR_INVALID_INPUT;
    safe_string_copy(result.error_message,
                     "Hotspot name contains invalid characters",
                     sizeof(result.error_message));
    return result;
  }

  // First stop the hotspot if it's running
  nmcli_stop_hotspot(hotspot_name);

  // Then delete the connection
  char command[256];
  snprintf(command, sizeof(command), "nmcli connection delete %s 2>&1",
           hotspot_name);

  return execute_nmcli_command(command);
}

static wterm_result_t nmcli_get_hotspot_status(const char *hotspot_name,
                                               hotspot_status_t *status) {
  if (!hotspot_name || !status) {
    return WTERM_ERROR_INVALID_INPUT;
  }

  // Validate hotspot name
  if (!validate_hotspot_name(hotspot_name)) {
    return WTERM_ERROR_INVALID_INPUT;
  }

  // Check if hotspot connection exists and get its state
  char command[256];
  snprintf(command, sizeof(command),
           "nmcli -t -f NAME,TYPE,STATE connection show %s 2>/dev/null",
           hotspot_name);

  FILE *fp = popen(command, "r");
  if (!fp) {
    return WTERM_ERROR_NETWORK;
  }

  char buffer[256];
  bool hotspot_found = false;

  if (fgets(buffer, sizeof(buffer), fp)) {
    buffer[strcspn(buffer, "\n")] = '\0';

    // Parse nmcli output: NAME:TYPE:STATE
    const char *name_field = strtok(buffer, ":");
    const char *type_field = strtok(NULL, ":");
    const char *state_field = strtok(NULL, ":");

    if (name_field && type_field && state_field &&
        strcmp(type_field, "wifi-hotspot") == 0) {
      hotspot_found = true;

      // Convert nmcli state to wterm hotspot state
      if (strcmp(state_field, "activated") == 0) {
        status->state = HOTSPOT_STATE_ACTIVE;
        safe_string_copy(status->status_message, "Hotspot is active",
                         sizeof(status->status_message));
      } else if (strcmp(state_field, "activating") == 0) {
        status->state = HOTSPOT_STATE_STARTING;
        safe_string_copy(status->status_message, "Hotspot is starting",
                         sizeof(status->status_message));
      } else if (strcmp(state_field, "deactivating") == 0) {
        status->state = HOTSPOT_STATE_STOPPING;
        safe_string_copy(status->status_message, "Hotspot is stopping",
                         sizeof(status->status_message));
      } else {
        status->state = HOTSPOT_STATE_STOPPED;
        safe_string_copy(status->status_message, "Hotspot is stopped",
                         sizeof(status->status_message));
      }
    }
  }

  pclose(fp);

  if (!hotspot_found) {
    status->state = HOTSPOT_STATE_STOPPED;
    safe_string_copy(status->status_message, "Hotspot configuration not found",
                     sizeof(status->status_message));
  }

  return WTERM_SUCCESS;
}

static wterm_result_t
nmcli_list_active_hotspots(char active_hotspots[][MAX_STR_SSID], int max_count,
                           int *count) {
  if (!active_hotspots || !count) {
    return WTERM_ERROR_INVALID_INPUT;
  }

  *count = 0;

  FILE *fp = popen(
      "nmcli -t -f NAME,TYPE,STATE connection show --active 2>/dev/null", "r");
  if (!fp) {
    return WTERM_ERROR_NETWORK;
  }

  char buffer[256];
  while (fgets(buffer, sizeof(buffer), fp) && *count < max_count) {
    buffer[strcspn(buffer, "\n")] = '\0';

    // Parse: NAME:TYPE:STATE
    const char *name_field = strtok(buffer, ":");
    const char *type_field = strtok(NULL, ":");
    const char *state_field = strtok(NULL, ":");

    if (name_field && type_field && state_field &&
        strcmp(type_field, "wifi-hotspot") == 0 &&
        strcmp(state_field, "activated") == 0) {
      safe_string_copy(active_hotspots[*count], name_field, MAX_STR_SSID);
      (*count)++;
    }
  }

  pclose(fp);
  return WTERM_SUCCESS;
}

static wterm_result_t nmcli_get_hotspot_clients(const char *hotspot_name,
                                                hotspot_client_t *clients,
                                                int max_clients,
                                                int *client_count) {
  if (!hotspot_name || !clients || !client_count) {
    return WTERM_ERROR_INVALID_INPUT;
  }

  *client_count = 0;

  // Get the interface associated with the hotspot
  char command[256];
  snprintf(
      command, sizeof(command),
      "nmcli -t -f connection.interface-name connection show %s 2>/dev/null",
      hotspot_name);

  FILE *fp = popen(command, "r");
  if (!fp) {
    return WTERM_ERROR_NETWORK;
  }

  char interface[MAX_STR_INTERFACE] = {0};
  if (fgets(interface, sizeof(interface), fp)) {
    interface[strcspn(interface, "\n")] = '\0';
  }
  pclose(fp);

  if (strlen(interface) == 0) {
    return WTERM_ERROR_GENERAL; // Could not find interface
  }

  // Use iw to get station information (requires root privileges)
  snprintf(command, sizeof(command), "iw dev %s station dump 2>/dev/null",
           interface);

  fp = popen(command, "r");
  if (!fp) {
    return WTERM_ERROR_NETWORK;
  }

  char buffer[256];
  while (fgets(buffer, sizeof(buffer), fp) && *client_count < max_clients) {
    buffer[strcspn(buffer, "\n")] = '\0';

    // Look for "Station" lines that contain MAC addresses
    if (strncmp(buffer, "Station ", 8) == 0) {
      char *mac_start = buffer + 8;
      char *space_pos = strchr(mac_start, ' ');
      if (space_pos) {
        *space_pos = '\0';
        safe_string_copy(clients[*client_count].mac_address, mac_start,
                         MAX_STR_MAC_ADDR);
        // Initialize other fields with default values
        safe_string_copy(clients[*client_count].hostname, "Unknown",
                         MAX_STR_DEVICE_NAME);
        safe_string_copy(clients[*client_count].ip_address, "Unknown",
                         MAX_STR_IP_ADDR);
        clients[*client_count].is_connected = true;
        clients[*client_count].bytes_sent = 0;
        clients[*client_count].bytes_received = 0;
        (*client_count)++;
      }
    }
  }

  pclose(fp);
  return WTERM_SUCCESS;
}

static wterm_result_t nmcli_check_interface_ap_support(const char *interface,
                                                       bool *supports_ap) {
  if (!interface || !supports_ap) {
    return WTERM_ERROR_INVALID_INPUT;
  }

  *supports_ap = false;

  // Check if interface supports AP mode by checking nmcli device capabilities
  // This is safer than using shell commands with grep
  FILE *fp = popen("nmcli -t -f DEVICE,TYPE device status", "r");
  if (!fp) {
    return WTERM_ERROR_NETWORK;
  }

  char buffer[256];
  while (fgets(buffer, sizeof(buffer), fp)) {
    buffer[strcspn(buffer, "\n")] = '\0';

    const char *device_field = strtok(buffer, ":");
    const char *type_field = strtok(NULL, ":");

    if (device_field && type_field && strcmp(device_field, interface) == 0 &&
        strcmp(type_field, "wifi") == 0) {
      // WiFi devices generally support AP mode
      *supports_ap = true;
      break;
    }
  }

  pclose(fp);
  return WTERM_SUCCESS;
}

static wterm_result_t
nmcli_get_available_wifi_interfaces(char interfaces[][MAX_STR_INTERFACE],
                                    int max_interfaces, int *interface_count) {
  if (!interfaces || !interface_count) {
    return WTERM_ERROR_INVALID_INPUT;
  }

  *interface_count = 0;

  FILE *fp = popen("nmcli -t -f DEVICE,TYPE device status 2>/dev/null", "r");
  if (!fp) {
    return WTERM_ERROR_NETWORK;
  }

  char buffer[128];
  while (fgets(buffer, sizeof(buffer), fp) &&
         *interface_count < max_interfaces) {
    buffer[strcspn(buffer, "\n")] = '\0';

    // Parse: DEVICE:TYPE
    const char *device_field = strtok(buffer, ":");
    const char *type_field = strtok(NULL, ":");

    if (device_field && type_field && strcmp(type_field, "wifi") == 0) {
      safe_string_copy(interfaces[*interface_count], device_field,
                       MAX_STR_INTERFACE);
      (*interface_count)++;
    }
  }

  pclose(fp);
  return WTERM_SUCCESS;
}
