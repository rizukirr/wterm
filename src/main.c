/**
 * @file main.c
 * @brief Main entry point for wterm WiFi management tool
 */

#include "../include/wterm/common.h"
#include "core/connection.h"
#include "core/hotspot_manager.h"
#include "core/hotspot_ui.h"
#include "core/network_scanner.h"
#include "fzf_ui.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static void print_usage(const char *program_name) {
  printf("Usage: %s [OPTION|COMMAND]\n\n", program_name);
  printf("WiFi network connection and hotspot management tool.\n\n");
  printf("Options:\n");
  printf("  -h, --help     Show this help message\n\n");
  printf("Commands:\n");
  printf("  hotspot        Manage WiFi hotspots\n");
  printf("  [no command]   Show network selection interface (default)\n\n");
  printf("Hotspot Commands:\n");
  printf("  hotspot menu            Interactive hotspot management menu\n");
  printf("  hotspot create          Create new hotspot interactively\n");
  printf("  hotspot start <name>    Start hotspot by name\n");
  printf("  hotspot stop <name>     Stop running hotspot\n");
  printf("  hotspot list            List all hotspot configurations\n");
  printf("  hotspot status [name]   Show hotspot status\n");
  printf("  hotspot delete <name>   Delete hotspot configuration\n");
  printf("  hotspot quick           Quick hotspot with default settings\n\n");
  printf("Network Interface:\n");
  printf("  ↑↓             Navigate networks\n");
  printf("  Enter          Connect to selected network\n");
  printf("  🔄 Rescan      Refresh network list\n");
  printf("  Type           Search networks\n");
  printf("  q/Esc          Quit\n\n");
  printf("Examples:\n");
  printf("  %s                           # Show network selection interface\n",
         program_name);
  printf("  %s hotspot create            # Create new hotspot interactively\n",
         program_name);
  printf("  %s hotspot start MyHotspot   # Start saved hotspot\n",
         program_name);
  printf("  %s hotspot list              # List all hotspots\n", program_name);
}

static wterm_result_t handle_list_networks(void) {
  network_list_t network_list;
  wterm_result_t result = scan_wifi_networks(&network_list);

  if (result != WTERM_SUCCESS) {
    fprintf(stderr, "Failed to scan WiFi networks\n");
    return result;
  }

  display_networks(&network_list);
  return WTERM_SUCCESS;
}

static wterm_result_t scan_networks_with_loading(network_list_t *network_list,
                                                 bool is_rescan) {
  const char *message =
      is_rescan ? "Rescanning networks..." : "Scanning networks...";

  show_loading_animation(message);

  wterm_result_t result;
  if (is_rescan) {
    // Use silent rescan to avoid verbose output
    result = rescan_wifi_networks_silent(true);
    if (result == WTERM_SUCCESS) {
      result = scan_wifi_networks(network_list);
    }
  } else {
    result = scan_wifi_networks(network_list);
  }

  hide_loading_animation();

  if (result != WTERM_SUCCESS) {
    fprintf(stderr, "Failed to scan WiFi networks\n");
  }

  return result;
}

static wterm_result_t handle_fzf_mode(void) {
  // Check if fzf is available
  if (!fzf_is_available()) {
    fprintf(stderr, "fzf not found - falling back to text mode\n");
    fprintf(stderr, "Install fzf for interactive selection: "
                    "https://github.com/junegunn/fzf\n");
    return handle_list_networks();
  }

  while (true) {
    // Scan networks with loading indicator
    network_list_t network_list;
    wterm_result_t result = scan_networks_with_loading(&network_list, false);
    if (result != WTERM_SUCCESS) {
      return result;
    }

    // Show fzf selection
    char selected_ssid[MAX_STR_SSID];
    if (!fzf_select_network_proper(&network_list, selected_ssid,
                                   sizeof(selected_ssid))) {
      fzf_show_message("No network selected.");
      return WTERM_SUCCESS;
    }

    // Check if user selected rescan
    if (strcmp(selected_ssid, "RESCAN") == 0) {
      result = scan_networks_with_loading(&network_list, true);
      if (result != WTERM_SUCCESS) {
        return result;
      }
      continue; // Show selection again with new networks
    }

    // Check if user selected hotspot manager
    if (strcmp(selected_ssid, "HOTSPOT") == 0) {
      hotspot_interactive_menu();
      continue; // Return to network selection after hotspot menu
    }

    // Find the selected network in our list
    network_info_t *selected_network = NULL;
    for (int i = 0; i < network_list.count; i++) {
      if (strcmp(network_list.networks[i].ssid, selected_ssid) == 0) {
        selected_network = &network_list.networks[i];
        break;
      }
    }

    if (!selected_network) {
      fprintf(stderr, "Selected network not found in scan results\n");
      return WTERM_ERROR_GENERAL;
    }

    // Handle connection
    connection_result_t conn_result;
    if (network_requires_password(selected_network->security)) {
      char password[256];
      if (!fzf_get_password(selected_ssid, password, sizeof(password))) {
        fzf_show_message("Connection cancelled.");
        return WTERM_SUCCESS;
      }

      fzf_show_message("Connecting...");
      conn_result = connect_to_secured_network(selected_ssid, password);

      // Clear password from memory
      memset(password, 0, sizeof(password));
    } else {
      fzf_show_message("Connecting to open network...");
      conn_result = connect_to_open_network(selected_ssid);
    }

    // Show result
    if (conn_result.result == WTERM_SUCCESS) {
      fzf_show_message("✓ Connected successfully!");
    } else {
      char error_msg[512];
      snprintf(error_msg, sizeof(error_msg), "✗ Connection failed: %s",
               conn_result.error_message);
      fzf_show_message(error_msg);
    }

    return conn_result.result;
  }
}

// Hotspot command handlers
static wterm_result_t handle_hotspot_list(void) {
  wterm_result_t result = hotspot_manager_init();
  if (result != WTERM_SUCCESS) {
    fprintf(stderr, "Failed to initialize hotspot manager\n");
    return result;
  }

  hotspot_list_t hotspot_list;
  result = hotspot_list_configs(&hotspot_list);
  if (result != WTERM_SUCCESS) {
    fprintf(stderr, "Failed to list hotspot configurations\n");
    hotspot_manager_cleanup();
    return result;
  }

  if (hotspot_list.count == 0) {
    printf("No hotspot configurations found.\n");
    printf("Use 'wterm hotspot create' to create a new hotspot.\n");
  } else {
    printf("Hotspot Configurations:\n");
    printf("%-20s %-20s %-10s %-10s\n", "Name", "SSID", "Interface",
           "Security");
    printf("%-20s %-20s %-10s %-10s\n", "----", "----", "---------",
           "--------");

    for (int i = 0; i < hotspot_list.count; i++) {
      const hotspot_config_t *config = &hotspot_list.hotspots[i];
      printf("%-20s %-20s %-10s %-10s\n", config->name, config->ssid,
             config->wifi_interface,
             hotspot_security_type_to_string(config->security_type));
    }
  }

  hotspot_manager_cleanup();
  return WTERM_SUCCESS;
}

static wterm_result_t handle_hotspot_start(const char *hotspot_name) {
  if (!hotspot_name) {
    fprintf(stderr, "Hotspot name required\n");
    return WTERM_ERROR_INVALID_INPUT;
  }

  wterm_result_t result = hotspot_manager_init();
  if (result != WTERM_SUCCESS) {
    fprintf(stderr, "Failed to initialize hotspot manager\n");
    return result;
  }

  hotspot_status_t status;
  result = hotspot_start(hotspot_name, &status);

  if (result == WTERM_SUCCESS) {
    printf("✓ Hotspot '%s' started successfully\n", hotspot_name);
    printf("SSID: %s\n", status.config.ssid);
    printf("Interface: %s\n", status.config.wifi_interface);
  } else {
    fprintf(stderr, "✗ Failed to start hotspot '%s'\n", hotspot_name);
  }

  hotspot_manager_cleanup();
  return result;
}

static wterm_result_t handle_hotspot_stop(const char *hotspot_name) {
  if (!hotspot_name) {
    fprintf(stderr, "Hotspot name required\n");
    return WTERM_ERROR_INVALID_INPUT;
  }

  wterm_result_t result = hotspot_manager_init();
  if (result != WTERM_SUCCESS) {
    fprintf(stderr, "Failed to initialize hotspot manager\n");
    return result;
  }

  result = hotspot_stop(hotspot_name);

  if (result == WTERM_SUCCESS) {
    printf("✓ Hotspot '%s' stopped successfully\n", hotspot_name);
  } else {
    fprintf(stderr, "✗ Failed to stop hotspot '%s'\n", hotspot_name);
  }

  hotspot_manager_cleanup();
  return result;
}

static wterm_result_t handle_hotspot_status(const char *hotspot_name) {
  wterm_result_t result = hotspot_manager_init();
  if (result != WTERM_SUCCESS) {
    fprintf(stderr, "Failed to initialize hotspot manager\n");
    return result;
  }

  if (hotspot_name) {
    // Show status for specific hotspot
    hotspot_status_t status;
    result = hotspot_get_status(hotspot_name, &status);

    if (result == WTERM_SUCCESS) {
      printf("Hotspot: %s\n", hotspot_name);
      printf("State: ");
      switch (status.state) {
      case HOTSPOT_STATE_ACTIVE:
        printf("Active ✓\n");
        break;
      case HOTSPOT_STATE_STARTING:
        printf("Starting...\n");
        break;
      case HOTSPOT_STATE_STOPPING:
        printf("Stopping...\n");
        break;
      case HOTSPOT_STATE_STOPPED:
        printf("Stopped\n");
        break;
      case HOTSPOT_STATE_ERROR:
        printf("Error\n");
        break;
      }
      printf("SSID: %s\n", status.config.ssid);
      printf("Interface: %s\n", status.config.wifi_interface);
      printf("Security: %s\n",
             hotspot_security_type_to_string(status.config.security_type));
      printf("Status: %s\n", status.status_message);
    } else {
      fprintf(stderr, "Failed to get status for hotspot '%s'\n", hotspot_name);
    }
  } else {
    // Show status for all active hotspots
    printf("Active hotspots status not yet implemented for all hotspots\n");
    printf("Use 'wterm hotspot status <name>' for specific hotspot status\n");
  }

  hotspot_manager_cleanup();
  return result;
}

static wterm_result_t handle_hotspot_delete(const char *hotspot_name) {
  if (!hotspot_name) {
    fprintf(stderr, "Hotspot name required\n");
    return WTERM_ERROR_INVALID_INPUT;
  }

  wterm_result_t result = hotspot_manager_init();
  if (result != WTERM_SUCCESS) {
    fprintf(stderr, "Failed to initialize hotspot manager\n");
    return result;
  }

  result = hotspot_delete_config(hotspot_name);

  if (result == WTERM_SUCCESS) {
    printf("✓ Hotspot configuration '%s' deleted successfully\n", hotspot_name);
  } else {
    fprintf(stderr, "✗ Failed to delete hotspot configuration '%s'\n",
            hotspot_name);
  }

  hotspot_manager_cleanup();
  return result;
}

static wterm_result_t handle_hotspot_quick(void) {
  wterm_result_t result = hotspot_manager_init();
  if (result != WTERM_SUCCESS) {
    fprintf(stderr, "Failed to initialize hotspot manager\n");
    return result;
  }

  // Use default values for quick setup
  hotspot_status_t status;
  result = hotspot_quick_start("wterm_quick", "wterm123456", "wlan0", "eth0",
                               &status);

  if (result == WTERM_SUCCESS) {
    printf("✓ Quick hotspot started successfully\n");
    printf("SSID: wterm_quick\n");
    printf("Password: wterm123456\n");
    printf("Interface: wlan0\n");
    printf("Sharing from: eth0\n");
  } else {
    fprintf(stderr, "✗ Failed to start quick hotspot\n");
    fprintf(stderr, "Make sure wlan0 and eth0 interfaces are available\n");
  }

  hotspot_manager_cleanup();
  return result;
}

static wterm_result_t handle_hotspot_create(void) {
  // Check if fzf is available
  if (!fzf_is_available()) {
    fprintf(stderr,
            "fzf not found - interactive hotspot creation requires fzf\n");
    fprintf(stderr, "Install fzf: https://github.com/junegunn/fzf\n");
    fprintf(stderr, "Or use 'wterm hotspot quick' for default settings\n");
    return WTERM_ERROR_GENERAL;
  }

  wterm_result_t result = hotspot_manager_init();
  if (result != WTERM_SUCCESS) {
    fprintf(stderr, "Failed to initialize hotspot manager\n");
    return result;
  }

  hotspot_config_t config;
  if (!fzf_create_hotspot_interactive(&config)) {
    printf("Hotspot creation cancelled.\n");
    hotspot_manager_cleanup();
    return WTERM_SUCCESS;
  }

  // Validate the configuration
  char error_msg[256];
  result = hotspot_validate_config(&config, error_msg, sizeof(error_msg));
  if (result != WTERM_SUCCESS) {
    fprintf(stderr, "Configuration error: %s\n", error_msg);
    hotspot_manager_cleanup();
    return result;
  }

  // Create the hotspot configuration
  result = hotspot_create_config(&config);
  if (result != WTERM_SUCCESS) {
    fprintf(stderr, "Failed to create hotspot configuration\n");

    // Provide more specific error information
    switch (result) {
    case WTERM_ERROR_NETWORK:
      fprintf(stderr, "Network error: Check if NetworkManager is running and "
                      "interface supports AP mode\n");
      break;
    case WTERM_ERROR_INVALID_INPUT:
      fprintf(stderr, "Invalid configuration: Check SSID length, password, and "
                      "interface names\n");
      break;
    case WTERM_ERROR_PERMISSION:
      fprintf(stderr, "Permission error: Try running with sudo or check "
                      "NetworkManager permissions\n");
      break;
    case WTERM_ERROR_GENERAL:
      fprintf(stderr, "A hotspot with this name may already exist. Try 'wterm "
                      "hotspot list' to check\n");
      break;
    default:
      fprintf(stderr, "Error code: %d\n", result);
      break;
    }

    fprintf(stderr, "\nTroubleshooting:\n");
    fprintf(
        stderr,
        "- Check that wlan1 supports Access Point mode: iw dev wlan1 info\n");
    fprintf(stderr, "- Verify NetworkManager version supports wifi-hotspot: "
                    "nmcli --version\n");
    fprintf(stderr, "- Try the quick command: wterm hotspot quick\n");

    hotspot_manager_cleanup();
    return result;
  }

  printf("✓ Hotspot configuration '%s' created successfully\n", config.name);
  printf("SSID: %s\n", config.ssid);
  printf("Interface: %s\n", config.wifi_interface);
  if (config.internet_interface[0]) {
    printf("Sharing from: %s\n", config.internet_interface);
  }

  // Ask if user wants to start the hotspot immediately
  printf("\nStart hotspot now? [y/N]: ");
  fflush(stdout);

  char response[10];
  if (fgets(response, sizeof(response), stdin)) {
    if (response[0] == 'y' || response[0] == 'Y') {
      hotspot_status_t status;
      result = hotspot_start(config.name, &status);
      if (result == WTERM_SUCCESS) {
        printf("✓ Hotspot '%s' started successfully\n", config.name);
      } else {
        fprintf(stderr, "✗ Failed to start hotspot '%s'\n", config.name);
      }
    }
  }

  hotspot_manager_cleanup();
  return result;
}

static wterm_result_t handle_hotspot_commands(int argc, char *argv[]) {
  if (argc < 3) {
    // No subcommand provided, show interactive menu by default
    return hotspot_interactive_menu();
  }

  const char *subcommand = argv[2];

  if (strcmp(subcommand, "menu") == 0) {
    return hotspot_interactive_menu();
  } else if (strcmp(subcommand, "list") == 0) {
    return handle_hotspot_list();
  } else if (strcmp(subcommand, "start") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Hotspot name required for start command\n");
      return WTERM_ERROR_INVALID_INPUT;
    }
    return handle_hotspot_start(argv[3]);
  } else if (strcmp(subcommand, "stop") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Hotspot name required for stop command\n");
      return WTERM_ERROR_INVALID_INPUT;
    }
    return handle_hotspot_stop(argv[3]);
  } else if (strcmp(subcommand, "status") == 0) {
    const char *name = (argc >= 4) ? argv[3] : NULL;
    return handle_hotspot_status(name);
  } else if (strcmp(subcommand, "delete") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Hotspot name required for delete command\n");
      return WTERM_ERROR_INVALID_INPUT;
    }
    return handle_hotspot_delete(argv[3]);
  } else if (strcmp(subcommand, "quick") == 0) {
    return handle_hotspot_quick();
  } else if (strcmp(subcommand, "create") == 0) {
    return handle_hotspot_create();
  } else {
    fprintf(stderr, "Unknown hotspot command: %s\n", subcommand);
    fprintf(stderr, "Use 'wterm hotspot' to see available commands\n");
    return WTERM_ERROR_INVALID_INPUT;
  }
}

int main(int argc, char *argv[]) {
  // Handle command line arguments
  if (argc > 1) {
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
      print_usage(argv[0]);
      return WTERM_SUCCESS;
    } else if (strcmp(argv[1], "hotspot") == 0) {
      // Handle hotspot commands
      return handle_hotspot_commands(argc, argv);
    } else {
      fprintf(stderr, "Unknown command: %s\n", argv[1]);
      fprintf(stderr, "Use --help for usage information.\n");
      return WTERM_ERROR_INVALID_INPUT;
    }
  }

  // Default action: show fzf interface
  wterm_result_t result = handle_fzf_mode();
  return result;
}
