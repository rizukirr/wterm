/**
 * @file connection.c
 * @brief WiFi connection management implementation
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE  // For explicit_bzero
#include "connection.h"
#include "../utils/input_sanitizer.h"
#include "../utils/safe_exec.h"
#include "../utils/string_utils.h"
#include "error_handler.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Global cancellation state for connection operations
// Using volatile sig_atomic_t for thread-safety and signal-safety
// Note: While sig_atomic_t is atomic, we use __atomic_* for proper memory ordering across pthreads
static volatile sig_atomic_t connection_cancelled = 0;

void init_connection_cancel(void) {
  __atomic_store_n(&connection_cancelled, 0, __ATOMIC_SEQ_CST);
}

void request_connection_cancel(void) {
  __atomic_store_n(&connection_cancelled, 1, __ATOMIC_SEQ_CST);
}

bool is_connection_cancelled(void) {
  return __atomic_load_n(&connection_cancelled, __ATOMIC_SEQ_CST) != 0;
}

// Helper function to check if a saved connection exists for the given SSID
static bool connection_exists(const char *ssid) {
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
bool is_saved_connection(const char *ssid) { return connection_exists(ssid); }

// Helper function to execute nmcli connection command and monitor result
static connection_result_t execute_nmcli_connect(const char *command,
                                                 const char *ssid) {
  connection_result_t result = {0};
  struct timespec sleep_time = {0, 100000000}; // 100ms

  // Execute nmcli command (runs in background)
  FILE *fp = popen(command, "r");
  if (!fp) {
    result.result = WTERM_ERROR_NETWORK;
    result.error_type = CONN_ERROR_NETWORKMANAGER_NOT_RUNNING;
    safe_string_copy(result.error_message, "Failed to execute nmcli command",
                     sizeof(result.error_message));
    return result;
  }

  // Close immediately - we'll monitor via connection status
  pclose(fp);

  // Poll connection status for up to 15 seconds (150 iterations * 100ms)
  for (int i = 0; i < 150; i++) {
    // Check for cancellation
    if (is_connection_cancelled()) {
      result.result = WTERM_ERROR_CANCELLED;
      safe_string_copy(result.error_message, "Connection cancelled by user",
                       sizeof(result.error_message));
      return result;
    }

    connection_status_t status = get_connection_status();

    // Check if connected to target SSID
    if (status.is_connected && strcmp(status.connected_ssid, ssid) == 0) {
      result.result = WTERM_SUCCESS;
      result.connected = true;
      snprintf(result.error_message, sizeof(result.error_message),
               "Successfully connected to %s", ssid);
      return result;
    }

    nanosleep(&sleep_time, NULL);
  }

  // Connection didn't establish in time
  result.result = WTERM_ERROR_NETWORK;
  result.error_type = CONN_ERROR_TIMEOUT;
  snprintf(result.error_message, sizeof(result.error_message),
           "Connection to %s timed out", ssid);
  result.connected = false;

  return result;
}


connection_result_t connect_to_open_network(const char *ssid) {
  connection_result_t result = {0};

  if (!ssid || is_string_empty(ssid)) {
    result.result = WTERM_ERROR_INVALID_INPUT;
    safe_string_copy(result.error_message, "Invalid SSID provided",
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

  // Initialize cancellation state for this connection attempt
  init_connection_cancel();

  // Escape SSID for shell safety
  char escaped_ssid[256];
  if (!shell_escape(ssid, escaped_ssid, sizeof(escaped_ssid))) {
    result.result = WTERM_ERROR_INVALID_INPUT;
    safe_string_copy(result.error_message, "SSID too long for shell escaping",
                     sizeof(result.error_message));
    return result;
  }

  char command[512];

  // Check if a saved connection exists
  if (connection_exists(ssid)) {
    // Use 'nmcli connection up' for existing connections
    snprintf(command, sizeof(command), "nmcli connection up %s 2>&1",
             escaped_ssid);
  } else {
    // Use 'nmcli device wifi connect' for new connections
    snprintf(command, sizeof(command), "nmcli device wifi connect %s 2>&1",
             escaped_ssid);
  }

  return execute_nmcli_connect(command, ssid);
}

connection_result_t connect_to_secured_network(const char *ssid,
                                               const char *password) {
  connection_result_t result = {0};

  if (!ssid || is_string_empty(ssid)) {
    result.result = WTERM_ERROR_INVALID_INPUT;
    safe_string_copy(result.error_message, "Invalid SSID provided",
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

  // Initialize cancellation state for this connection attempt
  init_connection_cancel();

  // Escape SSID for shell safety
  char escaped_ssid[256];
  if (!shell_escape(ssid, escaped_ssid, sizeof(escaped_ssid))) {
    result.result = WTERM_ERROR_INVALID_INPUT;
    safe_string_copy(result.error_message, "SSID too long for shell escaping",
                     sizeof(result.error_message));
    return result;
  }

  char command[1024];

  // Check if a saved connection exists for this SSID
  if (connection_exists(ssid)) {
    // Use 'nmcli connection up' for existing connections
    // Password is stored in the saved connection, so we don't need to pass it
    snprintf(command, sizeof(command), "nmcli connection up %s 2>&1",
             escaped_ssid);
  } else {
    // New connection - password is required
    if (!password || is_string_empty(password)) {
      result.result = WTERM_ERROR_INVALID_INPUT;
      safe_string_copy(result.error_message,
                       "Password required for secured network",
                       sizeof(result.error_message));
      return result;
    }

    // Escape password for shell safety
    char escaped_password[512];
    if (!shell_escape(password, escaped_password, sizeof(escaped_password))) {
      result.result = WTERM_ERROR_INVALID_INPUT;
      safe_string_copy(result.error_message,
                       "Password too long for shell escaping",
                       sizeof(result.error_message));
      return result;
    }

    // For new connections: Create connection profile first (stores password securely),
    // then activate it. This avoids exposing password in process list.
    // Step 1: Create connection profile
    char *const add_args[] = {
      "nmcli", "connection", "add",
      "type", "wifi",
      "con-name", escaped_ssid,
      "ssid", escaped_ssid,
      "wifi-sec.key-mgmt", "wpa-psk",
      "wifi-sec.psk", escaped_password,
      NULL
    };

    wterm_result_t add_result = safe_exec_check("nmcli", add_args);

    // Clear password from memory immediately after use
    explicit_bzero(escaped_password, sizeof(escaped_password));

    if (add_result != WTERM_SUCCESS) {
      result.result = WTERM_ERROR_NETWORK;
      safe_string_copy(result.error_message,
                       "Failed to create connection profile",
                       sizeof(result.error_message));
      return result;
    }

    // Step 2: Activate the connection (password now stored securely by NetworkManager)
    snprintf(command, sizeof(command), "nmcli connection up %s 2>&1",
             escaped_ssid);
  }

  return execute_nmcli_connect(command, ssid);
}

connection_status_t get_connection_status(void) {
  connection_status_t status = {0};

  // Get active connection profile name for WiFi
  FILE *fp =
      popen("nmcli -t -f NAME,TYPE,DEVICE connection show --active", "r");
  if (!fp) {
    return status;
  }

  char buffer[256];
  while (fgets(buffer, sizeof(buffer), fp)) {
    buffer[strcspn(buffer, "\n")] = '\0';

    // Parse line: NAME:TYPE:DEVICE
    char *first_colon = strchr(buffer, ':');
    if (!first_colon)
      continue;

    char *second_colon = strchr(first_colon + 1, ':');
    if (!second_colon)
      continue;

    // Extract fields
    *first_colon = '\0';
    *second_colon = '\0';
    const char *name = buffer;
    const char *type = first_colon + 1;

    // Check if this is a WiFi connection
    if (strcmp(type, "802-11-wireless") == 0) {
      safe_string_copy(status.connection_name, name,
                       sizeof(status.connection_name));
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
          safe_string_copy(status.connected_ssid, ssid_start,
                           sizeof(status.connected_ssid));
          break;
        }
      }
      pclose(fp);
    }
  }

  // Get IP address if connected
  if (status.is_connected) {
    fp = popen("nmcli -t -f IP4.ADDRESS connection show --active 2>/dev/null | "
               "head -1",
               "r");
    if (fp) {
      if (fgets(buffer, sizeof(buffer), fp)) {
        buffer[strcspn(buffer, "\n")] = '\0';
        // Extract IP address (remove prefix like "IP4.ADDRESS[1]:")
        const char *ip_start = strrchr(buffer, ':');
        if (ip_start && ip_start[1] != '\0') {
          safe_string_copy(status.ip_address, ip_start + 1,
                           sizeof(status.ip_address));
        } else if (strlen(buffer) > 0) {
          safe_string_copy(status.ip_address, buffer,
                           sizeof(status.ip_address));
        }
      }
      pclose(fp);
    }
  }

  return status;
}

wterm_result_t disconnect_current_network(void) {
  connection_status_t status = get_connection_status();

  // Disconnect active NetworkManager connection if one exists
  if (status.is_connected && status.connection_name[0] != '\0') {
    char *const args[] = {"nmcli", "connection", "down", status.connection_name,
                          NULL};
    return safe_exec_check("nmcli", args);
  }

  // No active connection to disconnect
  return WTERM_SUCCESS;
}

bool network_requires_password(const char *security) {
  if (!security || is_string_empty(security)) {
    return false; // Open network
  }

  // Check for common security types that require passwords
  return (strstr(security, "WPA") != NULL || strstr(security, "WEP") != NULL ||
          strstr(security, "Enterprise") != NULL);
}

bool validate_password(const char *password, const char *security) {
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

void secure_clear_password(char *password, size_t length) {
  if (password) {
    // Securely clear password from memory
    volatile char *p = password;
    for (size_t i = 0; i < length; i++) {
      p[i] = 0;
    }
  }
}

const char *get_signal_bars(const char *signal_str) {
  if (!signal_str) {
    return "    ";
  }

  int signal = atoi(signal_str);

  if (signal >= 80)
    return "▇▇▇▇▇";
  if (signal >= 60)
    return "▇▇▇▇▆";
  if (signal >= 40)
    return "▇▇▇▆▆";
  if (signal >= 20)
    return "▇▇▆▆▆";
  if (signal > 0)
    return "▇▆▆▆▆";

  return "▁▁▁▁▁";
}

bool is_connected_to_network(const char *ssid) {
  if (!ssid) {
    return false;
  }

  connection_status_t status = get_connection_status();
  return (status.is_connected && strcmp(status.connected_ssid, ssid) == 0);
}

connection_result_t monitor_connection_progress(const char *ssid,
                                                int timeout_seconds) {
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
