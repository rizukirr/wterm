/**
 * @file main.c
 * @brief Main entry point for wterm WiFi management tool
 */

#include "../include/wterm/common.h"
#include "../include/wterm/tui_interface.h"
#include "core/connection.h"
#include "core/hotspot_manager.h"
#include "core/hotspot_ui.h"
#include "core/network_scanner.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

  printf("%s\n", message);
  fflush(stdout);

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

  if (result != WTERM_SUCCESS) {
    fprintf(stderr, "Failed to scan WiFi networks\n");
  }

  return result;
}

static wterm_result_t handle_tui_mode(void) {
  // Check if TUI is available
  if (!tui_is_available()) {
    fprintf(stderr, "TUI not available - must run in a proper terminal (TTY)\n");
    return handle_list_networks();
  }

  // Do initial scan BEFORE initializing TUI (so loading messages show)
  network_list_t network_list;
  wterm_result_t result = scan_networks_with_loading(&network_list, false);
  if (result != WTERM_SUCCESS) {
    return result;
  }

  // Initialize TUI after scan completes
  if (tui_init() != WTERM_SUCCESS) {
    fprintf(stderr, "TUI initialization failed\n");
    return WTERM_ERROR_GENERAL;
  }

  while (true) {
    // Show network selection (TUI handles connections internally)
    char selected_ssid[MAX_STR_SSID];
    bool selection_made = tui_select_network(&network_list, selected_ssid,
                                            sizeof(selected_ssid));

    if (!selection_made) {
      // User quit
      tui_shutdown();
      return WTERM_SUCCESS;
    }

    // Check if user selected rescan
    if (strcmp(selected_ssid, "RESCAN") == 0) {
      // Clean up TUI before rescanning
      tui_shutdown();

      // Rescan (loading message will show because TUI is shut down)
      result = scan_networks_with_loading(&network_list, true);
      if (result != WTERM_SUCCESS) {
        return result;
      }

      // Reinitialize TUI after scan completes
      if (tui_init() != WTERM_SUCCESS) {
        fprintf(stderr, "Failed to reinitialize TUI\n");
        return WTERM_ERROR_GENERAL;
      }
      continue; // Show selection again with new networks
    }

    // Check if user selected hotspot manager
    if (strcmp(selected_ssid, "HOTSPOT") == 0) {
      // Clean up TUI before launching hotspot menu
      tui_shutdown();

      // Create a minimal argv for hotspot-only mode
      // Pass skip_elevation=true since we're already in the interactive session
      char *hotspot_argv[] = {"wterm", "hotspot", NULL};
      hotspot_interactive_menu(2, hotspot_argv, true);

      // Reinitialize TUI for next selection
      if (tui_init() != WTERM_SUCCESS) {
        fprintf(stderr, "Failed to reinitialize TUI\n");
        return WTERM_ERROR_GENERAL;
      }
      continue; // Return to network selection after hotspot menu
    }

    // For TUI mode, connections are handled internally
    // Loop continues to show network list again
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
  fprintf(stderr, "Interactive hotspot creation is available via TUI mode.\n");
  fprintf(stderr, "Use 'wterm hotspot' to access the interactive menu, or\n");
  fprintf(stderr, "Use 'wterm hotspot quick' for quick setup with defaults.\n");
  return WTERM_ERROR_GENERAL;
}

static wterm_result_t handle_hotspot_commands(int argc, char *argv[]) {
  if (argc < 3) {
    // No subcommand provided, show interactive menu by default
    // Pass skip_elevation=false to allow sudo elevation from command line
    return hotspot_interactive_menu(argc, argv, false);
  }

  const char *subcommand = argv[2];

  if (strcmp(subcommand, "menu") == 0) {
    // Pass skip_elevation=false to allow sudo elevation from command line
    return hotspot_interactive_menu(argc, argv, false);
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

  // Default action: show TUI/fzf interface
  wterm_result_t result = handle_tui_mode();
  return result;
}
