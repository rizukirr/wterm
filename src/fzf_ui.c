/**
 * @file fzf_ui.c
 * @brief fzf-based user interface implementation
 */

#define _GNU_SOURCE
#include "fzf_ui.h"
#include "utils/string_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>

bool fzf_is_available(void) {
  // Check if fzf command is available
  int result = system("command -v fzf >/dev/null 2>&1");
  return (result == 0);
}

// Removed redundant wrapper function - use fzf_select_network_proper directly

bool fzf_get_password(const char *ssid, char *password_out,
                      size_t buffer_size) {
  if (!ssid || !password_out || buffer_size == 0) {
    return false;
  }

  printf("Connect to: %s\n", ssid);
  printf("Enter password: ");
  fflush(stdout);

  // Use getpass for hidden password input, fallback to fgets
  char *password = getpass("");
  if (password && strlen(password) > 0) {
    safe_string_copy(password_out, password, buffer_size);

    // Clear password from memory
    memset(password, 0, strlen(password));
    return true;
  }

  return false;
}

void fzf_show_message(const char *message) {
  if (message) {
    printf("%s\n", message);
  }
}

// Static variables for animation state
static int spinner_state = 0;
static bool animation_running = false;

// Show loading animation during scan
void show_loading_animation(const char *message) {
  const char *spinner[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
  const int spinner_len = 10;

  if (!animation_running) {
    animation_running = true;
    spinner_state = 0;
  }

  printf("\r%s %s", spinner[spinner_state % spinner_len], message);
  fflush(stdout);

  spinner_state++;
}

void hide_loading_animation(void) {
  animation_running = false;
  spinner_state = 0;
  printf("\r\033[K"); // Clear current line
  fflush(stdout);
}

// Enhanced implementation with rescan option
bool fzf_select_network_proper(const network_list_t *networks,
                               char *selected_ssid, size_t buffer_size) {
  if (!networks || !selected_ssid || buffer_size == 0) {
    return false;
  }

  if (networks->count == 0) {
    printf("No WiFi networks found.\n");
    return false;
  }

  // Create temporary file with network list
  // Use secure temporary directory if available
  const char *tmpdir = getenv("TMPDIR");
  if (!tmpdir || strlen(tmpdir) == 0) {
    tmpdir = "/tmp";
  }

  char temp_file[256];
  snprintf(temp_file, sizeof(temp_file), "%s/wterm_networks_XXXXXX", tmpdir);

  int fd = mkstemp(temp_file);
  if (fd == -1) {
    perror("Failed to create temporary file");
    return false;
  }

  // Set restrictive permissions (owner read/write only)
  if (fchmod(fd, 0600) != 0) {
    perror("Failed to set file permissions");
    close(fd);
    unlink(temp_file);
    return false;
  }

  FILE *temp = fdopen(fd, "w");
  if (!temp) {
    close(fd);
    unlink(temp_file);
    return false;
  }

  // Add special options as first items
  fprintf(temp, "🔄 Rescan networks\n");
  fprintf(temp, "📡 Hotspot Manager\n");
  fprintf(temp, "\n");

  // Write networks to temp file
  for (int i = 0; i < networks->count; i++) {
    fprintf(temp, "%s\n", networks->networks[i].ssid);
  }
  fclose(temp);

  // Build fzf command with header
  char fzf_command[512];
  snprintf(fzf_command, sizeof(fzf_command),
           "fzf --border --prompt='Select WiFi: ' --height=12 --reverse "
           "--header='↑↓ Navigate | Enter: Select | Type: Search | q/Esc: "
           "Quit' < %s",
           temp_file);

  // Run fzf and get selection
  FILE *fzf_output = popen(fzf_command, "r");
  bool success = false;

  if (fzf_output) {
    char selection[256] = {0};
    if (fgets(selection, sizeof(selection), fzf_output)) {
      // Remove trailing newline
      selection[strcspn(selection, "\n")] = '\0';

      if (strlen(selection) > 0) {
        // Check if user selected special options
        if (strcmp(selection, "🔄 Rescan networks") == 0) {
          safe_string_copy(selected_ssid, "RESCAN", buffer_size);
        } else if (strcmp(selection, "📡 Hotspot Manager") == 0) {
          safe_string_copy(selected_ssid, "HOTSPOT", buffer_size);
        } else {
          safe_string_copy(selected_ssid, selection, buffer_size);
        }
        success = true;
      }
    }
    pclose(fzf_output);
  }

  // Cleanup
  unlink(temp_file);
  return success;
}

// Hotspot creation implementation

bool fzf_select_wifi_interface(char *interface_out, size_t buffer_size) {
  if (!interface_out || buffer_size == 0) {
    return false;
  }

  wterm_result_t result = hotspot_manager_init();
  if (result != WTERM_SUCCESS) {
    fzf_show_message("Failed to initialize hotspot manager");
    return false;
  }

  // This would use the backend function, but for now we'll use a simple
  // approach Get interfaces using nmcli
  const char *tmpdir = getenv("TMPDIR");
  if (!tmpdir || strlen(tmpdir) == 0) {
    tmpdir = "/tmp";
  }

  char temp_file[256];
  snprintf(temp_file, sizeof(temp_file), "%s/wterm_interfaces_XXXXXX", tmpdir);

  int fd = mkstemp(temp_file);
  if (fd == -1) {
    hotspot_manager_cleanup();
    return false;
  }

  // Set restrictive permissions
  if (fchmod(fd, 0600) != 0) {
    close(fd);
    unlink(temp_file);
    hotspot_manager_cleanup();
    return false;
  }

  FILE *temp = fdopen(fd, "w");
  if (!temp) {
    close(fd);
    unlink(temp_file);
    hotspot_manager_cleanup();
    return false;
  }

  // Run nmcli to get WiFi devices
  FILE *nmcli_output =
      popen("nmcli -t -f DEVICE,TYPE device status 2>/dev/null", "r");
  if (nmcli_output) {
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), nmcli_output)) {
      buffer[strcspn(buffer, "\n")] = '\0';

      // Parse: DEVICE:TYPE
      char *device_field = strtok(buffer, ":");
      char *type_field = strtok(NULL, ":");

      if (device_field && type_field && strcmp(type_field, "wifi") == 0) {
        // Check if interface supports AP mode (simplified check)
        fprintf(temp, "%s (WiFi)\n", device_field);
      }
    }
    pclose(nmcli_output);
  }
  fclose(temp);

  // Build fzf command
  char fzf_command[512];
  snprintf(
      fzf_command, sizeof(fzf_command),
      "fzf --border --prompt='Select WiFi Interface: ' --height=8 --reverse "
      "--header='Step 1/4: Choose WiFi adapter for hotspot' < %s",
      temp_file);

  // Run fzf and get selection
  FILE *fzf_output = popen(fzf_command, "r");
  bool success = false;

  if (fzf_output) {
    char selection[256] = {0};
    if (fgets(selection, sizeof(selection), fzf_output)) {
      selection[strcspn(selection, "\n")] = '\0';

      if (strlen(selection) > 0) {
        // Extract interface name (remove description)
        char *space_pos = strchr(selection, ' ');
        if (space_pos) {
          *space_pos = '\0';
        }
        safe_string_copy(interface_out, selection, buffer_size);
        success = true;
      }
    }
    pclose(fzf_output);
  }

  // Cleanup
  unlink(temp_file);
  hotspot_manager_cleanup();
  return success;
}

bool fzf_select_internet_source(char *interface_out, size_t buffer_size) {
  if (!interface_out || buffer_size == 0) {
    return false;
  }

  // Create temporary file with internet source options
  const char *tmpdir = getenv("TMPDIR");
  if (!tmpdir || strlen(tmpdir) == 0) {
    tmpdir = "/tmp";
  }

  char temp_file[256];
  snprintf(temp_file, sizeof(temp_file), "%s/wterm_inet_sources_XXXXXX", tmpdir);

  int fd = mkstemp(temp_file);
  if (fd == -1) {
    return false;
  }

  // Set restrictive permissions
  if (fchmod(fd, 0600) != 0) {
    close(fd);
    unlink(temp_file);
    return false;
  }

  FILE *temp = fdopen(fd, "w");
  if (!temp) {
    close(fd);
    unlink(temp_file);
    return false;
  }

  // Add "No internet sharing" option
  fprintf(temp, "none (No internet sharing)\n");

  // Get available network connections
  FILE *nmcli_output = popen(
      "nmcli -t -f NAME,TYPE,STATE connection show --active 2>/dev/null", "r");
  if (nmcli_output) {
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), nmcli_output)) {
      buffer[strcspn(buffer, "\n")] = '\0';

      // Parse: NAME:TYPE:STATE
      char *name_field = strtok(buffer, ":");
      char *type_field = strtok(NULL, ":");
      char *state_field = strtok(NULL, ":");

      if (name_field && type_field && state_field &&
          strcmp(state_field, "activated") == 0) {
        fprintf(temp, "%s (%s)\n", name_field, type_field);
      }
    }
    pclose(nmcli_output);
  }

  // Also check for ethernet interfaces
  FILE *eth_output =
      popen("nmcli -t -f DEVICE,TYPE,STATE device status 2>/dev/null", "r");
  if (eth_output) {
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), eth_output)) {
      buffer[strcspn(buffer, "\n")] = '\0';

      // Parse: DEVICE:TYPE:STATE
      char *device_field = strtok(buffer, ":");
      char *type_field = strtok(NULL, ":");
      char *state_field = strtok(NULL, ":");

      if (device_field && type_field && state_field &&
          strcmp(type_field, "ethernet") == 0 &&
          strcmp(state_field, "connected") == 0) {
        fprintf(temp, "%s (Ethernet)\n", device_field);
      }
    }
    pclose(eth_output);
  }

  fclose(temp);

  // Build fzf command
  char fzf_command[512];
  snprintf(fzf_command, sizeof(fzf_command),
           "fzf --border --prompt='Internet Source: ' --height=10 --reverse "
           "--header='Step 2/4: Choose internet connection to share' < %s",
           temp_file);

  // Run fzf and get selection
  FILE *fzf_output = popen(fzf_command, "r");
  bool success = false;

  if (fzf_output) {
    char selection[256] = {0};
    if (fgets(selection, sizeof(selection), fzf_output)) {
      selection[strcspn(selection, "\n")] = '\0';

      if (strlen(selection) > 0) {
        if (strncmp(selection, "none", 4) == 0) {
          interface_out[0] = '\0'; // Empty string for no sharing
        } else {
          // Extract interface name (remove description in parentheses)
          char *paren_pos = strchr(selection, '(');
          if (paren_pos) {
            // Remove trailing spaces before the parentheses
            while (paren_pos > selection && *(paren_pos - 1) == ' ') {
              paren_pos--;
            }
            *paren_pos = '\0';
          }
          safe_string_copy(interface_out, selection, buffer_size);
        }
        success = true;
      }
    }
    pclose(fzf_output);
  }

  // Cleanup
  unlink(temp_file);
  return success;
}

bool fzf_get_text_input(const char *prompt, const char *default_value,
                        char *output_buffer, size_t buffer_size) {
  if (!prompt || !output_buffer || buffer_size == 0) {
    return false;
  }

  printf("%s", prompt);
  if (default_value && strlen(default_value) > 0) {
    printf(" [%s]: ", default_value);
  } else {
    printf(": ");
  }
  fflush(stdout);

  char input[256];
  if (fgets(input, sizeof(input), stdin)) {
    input[strcspn(input, "\n")] = '\0'; // Remove newline

    if (strlen(input) == 0 && default_value) {
      // Use default value if no input provided
      safe_string_copy(output_buffer, default_value, buffer_size);
    } else if (strlen(input) > 0) {
      safe_string_copy(output_buffer, input, buffer_size);
    } else {
      return false; // No input and no default
    }
    return true;
  }

  return false;
}

bool fzf_get_secure_input(const char *prompt, char *output_buffer,
                          size_t buffer_size) {
  if (!prompt || !output_buffer || buffer_size == 0) {
    return false;
  }

  printf("%s: ", prompt);
  fflush(stdout);

  // Use getpass for hidden input
  char *password = getpass("");
  if (password && strlen(password) > 0) {
    safe_string_copy(output_buffer, password, buffer_size);
    // Clear password from memory
    memset(password, 0, strlen(password));
    return true;
  }

  return false;
}

bool fzf_get_hotspot_config(hotspot_config_t *config) {
  if (!config) {
    return false;
  }

  printf("\n=== Step 3/4: Hotspot Configuration ===\n");

  // Get hotspot name
  if (!fzf_get_text_input("Hotspot name", "MyHotspot", config->name,
                          sizeof(config->name))) {
    return false;
  }

  // Get SSID (default to same as name)
  if (!fzf_get_text_input("Network SSID", config->name, config->ssid,
                          sizeof(config->ssid))) {
    return false;
  }

  // Get security type selection
  const char *tmpdir = getenv("TMPDIR");
  if (!tmpdir || strlen(tmpdir) == 0) {
    tmpdir = "/tmp";
  }

  char temp_file[256];
  snprintf(temp_file, sizeof(temp_file), "%s/wterm_security_XXXXXX", tmpdir);

  int fd = mkstemp(temp_file);
  if (fd == -1) {
    return false;
  }

  // Set restrictive permissions
  if (fchmod(fd, 0600) != 0) {
    close(fd);
    unlink(temp_file);
    return false;
  }

  FILE *temp = fdopen(fd, "w");
  if (!temp) {
    close(fd);
    unlink(temp_file);
    return false;
  }

  fprintf(temp, "WPA2 (Recommended)\n");
  fprintf(temp, "WPA3 (Most secure)\n");
  fprintf(temp, "Open (No password)\n");
  fclose(temp);

  char fzf_command[512];
  snprintf(fzf_command, sizeof(fzf_command),
           "fzf --border --prompt='Security: ' --height=6 --reverse "
           "--header='Choose security type' < %s",
           temp_file);

  FILE *fzf_output = popen(fzf_command, "r");
  char security_selection[64] = {0};
  bool security_selected = false;

  if (fzf_output) {
    if (fgets(security_selection, sizeof(security_selection), fzf_output)) {
      security_selection[strcspn(security_selection, "\n")] = '\0';
      security_selected = true;
    }
    pclose(fzf_output);
  }
  unlink(temp_file);

  if (!security_selected) {
    return false;
  }

  // Set security type and get password if needed
  if (strncmp(security_selection, "WPA2", 4) == 0) {
    config->security_type = WIFI_SECURITY_WPA2;
    if (!fzf_get_secure_input("Password (8-63 characters)", config->password,
                              sizeof(config->password))) {
      return false;
    }
  } else if (strncmp(security_selection, "WPA3", 4) == 0) {
    config->security_type = WIFI_SECURITY_WPA3;
    if (!fzf_get_secure_input("Password (8-63 characters)", config->password,
                              sizeof(config->password))) {
      return false;
    }
  } else {
    config->security_type = WIFI_SECURITY_NONE;
    config->password[0] = '\0';
  }

  return true;
}

bool fzf_confirm_hotspot_config(const hotspot_config_t *config) {
  if (!config) {
    return false;
  }

  printf("\n=== Step 4/4: Configuration Review ===\n");
  printf("Hotspot Name: %s\n", config->name);
  printf("SSID: %s\n", config->ssid);
  printf("WiFi Interface: %s\n", config->wifi_interface);
  printf("Internet Source: %s\n",
         config->internet_interface[0] ? config->internet_interface : "None");
  printf("Security: %s\n",
         hotspot_security_type_to_string(config->security_type));

  if (config->security_type != WIFI_SECURITY_NONE) {
    printf("Password: [Hidden]\n");
  }

  const char *tmpdir = getenv("TMPDIR");
  if (!tmpdir || strlen(tmpdir) == 0) {
    tmpdir = "/tmp";
  }

  char temp_file[256];
  snprintf(temp_file, sizeof(temp_file), "%s/wterm_confirm_XXXXXX", tmpdir);

  int fd = mkstemp(temp_file);
  if (fd == -1) {
    return false;
  }

  // Set restrictive permissions
  if (fchmod(fd, 0600) != 0) {
    close(fd);
    unlink(temp_file);
    return false;
  }

  FILE *temp = fdopen(fd, "w");
  if (!temp) {
    close(fd);
    unlink(temp_file);
    return false;
  }

  fprintf(temp, "✓ Create hotspot\n");
  fprintf(temp, "↩ Go back and modify\n");
  fprintf(temp, "✗ Cancel\n");
  fclose(temp);

  char fzf_command[512];
  snprintf(fzf_command, sizeof(fzf_command),
           "fzf --border --prompt='Confirm: ' --height=6 --reverse "
           "--header='Review configuration above' < %s",
           temp_file);

  FILE *fzf_output = popen(fzf_command, "r");
  bool confirmed = false;

  if (fzf_output) {
    char selection[64] = {0};
    if (fgets(selection, sizeof(selection), fzf_output)) {
      selection[strcspn(selection, "\n")] = '\0';
      confirmed = (strncmp(selection, "✓", 1) == 0);
    }
    pclose(fzf_output);
  }

  unlink(temp_file);
  return confirmed;
}

bool fzf_create_hotspot_interactive(hotspot_config_t *config) {
  if (!config) {
    return false;
  }

  // Initialize with defaults
  hotspot_get_default_config(config);

  printf("🔥 WiFi Hotspot Creation Wizard\n");
  printf("================================\n");

  // Step 1: Select WiFi interface
  if (!fzf_select_wifi_interface(config->wifi_interface,
                                 sizeof(config->wifi_interface))) {
    fzf_show_message("Cancelled: No WiFi interface selected");
    return false;
  }

  // Step 2: Select internet source
  if (!fzf_select_internet_source(config->internet_interface,
                                  sizeof(config->internet_interface))) {
    fzf_show_message("Cancelled: No internet source selected");
    return false;
  }

  // Set sharing method based on internet source
  if (config->internet_interface[0] == '\0') {
    config->share_method = HOTSPOT_SHARE_NONE;
  } else {
    config->share_method = HOTSPOT_SHARE_NAT;
  }

  // Step 3: Get configuration
  if (!fzf_get_hotspot_config(config)) {
    fzf_show_message("Cancelled: Configuration incomplete");
    return false;
  }

  // Step 4: Confirm
  if (!fzf_confirm_hotspot_config(config)) {
    fzf_show_message("Cancelled: Configuration not confirmed");
    return false;
  }

  return true;
}
