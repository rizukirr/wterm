/**
 * @file hotspot_ui.c
 * @brief Interactive hotspot management UI implementation (Pure C - No bash scripts)
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "hotspot_ui.h"
#include "hotspot_manager.h"
#include "../../include/wterm/common.h"
#include "../utils/safe_exec.h"
#include "../utils/input_sanitizer.h"
#include "../utils/string_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>  // For explicit_bzero
#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#define MAX_BUFFER 1024
#define MAX_COMMAND 2048
#define HOTSPOT_PASSWORD_MAX 64

// Helper to check if running as root
static bool is_root(void) {
    return (geteuid() == 0);
}


// Helper to get user confirmation
static bool get_confirmation(const char* message) {
    printf("%s [y/N]: ", message);
    fflush(stdout);

    char response[10];
    if (fgets(response, sizeof(response), stdin) == NULL) {
        return false;
    }

    return (response[0] == 'y' || response[0] == 'Y');
}

// Helper to get password input (hidden)
static bool get_password_input(const char* prompt, char* password, size_t size) {
    if (!password || size == 0) {
        return false;
    }

    struct termios old_term, new_term;

    printf("%s: ", prompt);
    fflush(stdout);

    // Disable echo
    if (tcgetattr(STDIN_FILENO, &old_term) != 0) {
        return false;
    }

    new_term = old_term;
    new_term.c_lflag &= ~ECHO;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &new_term) != 0) {
        return false;
    }

    // Read password
    bool success = (fgets(password, size, stdin) != NULL);
    printf("\n");

    // Restore echo
    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);

    if (success) {
        password[strcspn(password, "\n")] = '\0'; // Remove newline
    }

    return success;
}

// Select a hotspot from list using simple text menu
bool hotspot_select(const char* filter, char* selected_name, size_t buffer_size) {
    (void)filter; // Reserved for future filtering support

    if (!selected_name || buffer_size == 0) {
        return false;
    }

    // Get hotspot list from C function
    hotspot_list_t list;
    if (hotspot_list_configs(&list) != WTERM_SUCCESS || list.count == 0) {
        fprintf(stderr, "No hotspots configured\n");
        return false;
    }

    // Display hotspots
    printf("\nAvailable hotspots:\n");
    for (int i = 0; i < list.count; i++) {
        printf("  %d. %s\n", i + 1, list.hotspots[i].name);
    }

    printf("\nSelect hotspot [1-%d] (0 to cancel): ", list.count);
    fflush(stdout);

    int choice;
    if (scanf("%d", &choice) != 1) {
        fprintf(stderr, "Invalid input\n");
        return false;
    }
    getchar(); // Consume newline

    if (choice == 0) {
        return false; // Cancelled
    }

    if (choice < 1 || choice > list.count) {
        fprintf(stderr, "Invalid selection\n");
        return false;
    }

    safe_string_copy(selected_name, list.hotspots[choice - 1].name, buffer_size);
    return true;
}

// Create hotspot wizard
int hotspot_create_wizard(void) {
    printf("\n=== Create New Hotspot ===\n\n");

    hotspot_config_t config;
    hotspot_get_default_config(&config);

    // Step 1: Hotspot name
    char name[MAX_STR_SSID];
    printf("Hotspot name: ");
    fflush(stdout);
    if (fgets(name, sizeof(name), stdin) == NULL) {
        fprintf(stderr, "Failed to read hotspot name\n");
        return -1;
    }
    name[strcspn(name, "\n")] = '\0';

    if (!validate_hotspot_name(name)) {
        fprintf(stderr, "Invalid hotspot name\n");
        return -1;
    }

    safe_string_copy(config.name, name, sizeof(config.name));
    safe_string_copy(config.ssid, name, sizeof(config.ssid));

    // Step 2: WiFi interface selection
    interface_info_t interfaces[8];
    int iface_count = 0;

    if (hotspot_get_interface_list(interfaces, 8, &iface_count) != WTERM_SUCCESS || iface_count == 0) {
        fprintf(stderr, "No WiFi interfaces found\n");
        return -1;
    }

    printf("\nAvailable WiFi interfaces:\n");
    for (int i = 0; i < iface_count; i++) {
        printf("  %d. %s (%s)\n", i + 1, interfaces[i].name, interfaces[i].status);
    }

    printf("Select interface [1-%d]: ", iface_count);
    fflush(stdout);

    int iface_choice;
    if (scanf("%d", &iface_choice) != 1 || iface_choice < 1 || iface_choice > iface_count) {
        fprintf(stderr, "Invalid selection\n");
        return -1;
    }
    getchar(); // Consume newline

    safe_string_copy(config.wifi_interface, interfaces[iface_choice - 1].name,
                    sizeof(config.wifi_interface));

    // Step 3: Security selection
    security_option_t sec_options[2];
    int sec_count = 0;
    hotspot_get_security_options(sec_options, &sec_count);

    printf("\nSecurity:\n");
    for (int i = 0; i < sec_count; i++) {
        printf("  %d. %s\n", i + 1, sec_options[i].display);
    }

    printf("Select security [1-%d]: ", sec_count);
    fflush(stdout);

    int sec_choice;
    if (scanf("%d", &sec_choice) != 1 || sec_choice < 1 || sec_choice > sec_count) {
        fprintf(stderr, "Invalid selection\n");
        return -1;
    }
    getchar(); // Consume newline

    bool is_open = (strcmp(sec_options[sec_choice - 1].id, "open") == 0);
    config.security_type = is_open ? WIFI_SECURITY_NONE : WIFI_SECURITY_WPA2;

    // Step 4: Password (if secured)
    if (!is_open) {
        char password[HOTSPOT_PASSWORD_MAX];
        char password_confirm[HOTSPOT_PASSWORD_MAX];

        if (!get_password_input("Password", password, sizeof(password))) {
            fprintf(stderr, "Failed to read password\n");
            return -1;
        }

        if (strlen(password) < 8 || strlen(password) > 63) {
            fprintf(stderr, "Password must be 8-63 characters\n");
            explicit_bzero(password, sizeof(password));
            return -1;
        }

        if (!get_password_input("Confirm password", password_confirm, sizeof(password_confirm))) {
            fprintf(stderr, "Failed to confirm password\n");
            explicit_bzero(password, sizeof(password));
            return -1;
        }

        if (strcmp(password, password_confirm) != 0) {
            fprintf(stderr, "Passwords do not match\n");
            explicit_bzero(password, sizeof(password));
            explicit_bzero(password_confirm, sizeof(password_confirm));
            return -1;
        }

        safe_string_copy(config.password, password, sizeof(config.password));
        explicit_bzero(password, sizeof(password));
        explicit_bzero(password_confirm, sizeof(password_confirm));
    }

    // Step 5: Band selection
    band_option_t bands[2];
    int band_count = 0;
    hotspot_get_band_options(bands, &band_count);

    printf("\nFrequency band:\n");
    for (int i = 0; i < band_count; i++) {
        printf("  %d. %s\n", i + 1, bands[i].display);
    }

    printf("Select band [1-%d]: ", band_count);
    fflush(stdout);

    int band_choice;
    if (scanf("%d", &band_choice) != 1 || band_choice < 1 || band_choice > band_count) {
        fprintf(stderr, "Invalid selection\n");
        return -1;
    }
    getchar(); // Consume newline

    config.is_5ghz = (strcmp(bands[band_choice - 1].id, "a") == 0);

    // Create and start hotspot
    printf("\nCreating hotspot '%s'...\n", config.name);

    if (hotspot_create_config(&config) != WTERM_SUCCESS) {
        fprintf(stderr, "Failed to create hotspot configuration\n");
        return -1;
    }

    printf("Starting hotspot...\n");

    hotspot_status_t status;
    if (hotspot_start(config.name, &status) != WTERM_SUCCESS) {
        fprintf(stderr, "Failed to start hotspot\n");
        return -1;
    }

    printf("\n✓ Hotspot '%s' started successfully!\n", config.name);
    printf("  SSID: %s\n", config.ssid);
    printf("  Security: %s\n", is_open ? "Open" : "WPA2-PSK");
    printf("  Band: %s\n", config.is_5ghz ? "5GHz" : "2.4GHz");

    return 0;
}

// Start a hotspot interactively
int hotspot_start_interactive(void) {
    char name[MAX_STR_SSID];
    if (!hotspot_select(NULL, name, sizeof(name))) {
        fprintf(stderr, "No hotspot selected\n");
        return -1;
    }

    printf("Starting hotspot '%s'...\n", name);

    hotspot_status_t status;
    if (hotspot_start(name, &status) != WTERM_SUCCESS) {
        fprintf(stderr, "Failed to start hotspot\n");
        return -1;
    }

    printf("✓ Hotspot started successfully\n");
    return 0;
}

// Stop a hotspot interactively
int hotspot_stop_interactive(void) {
    char name[MAX_STR_SSID];
    if (!hotspot_select(NULL, name, sizeof(name))) {
        fprintf(stderr, "No hotspot selected\n");
        return -1;
    }

    printf("Stopping hotspot '%s'...\n", name);

    if (hotspot_stop(name) != WTERM_SUCCESS) {
        fprintf(stderr, "Failed to stop hotspot\n");
        return -1;
    }

    printf("✓ Hotspot stopped successfully\n");
    return 0;
}

// Delete a hotspot interactively
int hotspot_delete_interactive(void) {
    char name[MAX_STR_SSID];
    if (!hotspot_select(NULL, name, sizeof(name))) {
        fprintf(stderr, "No hotspot selected\n");
        return -1;
    }

    char prompt[128];
    snprintf(prompt, sizeof(prompt), "Delete hotspot '%s'?", name);

    if (!get_confirmation(prompt)) {
        printf("Cancelled\n");
        return 0;
    }

    if (hotspot_delete_config(name) != WTERM_SUCCESS) {
        fprintf(stderr, "Failed to delete hotspot\n");
        return -1;
    }

    printf("✓ Hotspot '%s' deleted\n", name);
    return 0;
}

// List all hotspots
int hotspot_list_all(void) {
    hotspot_list_t list;
    if (hotspot_list_configs(&list) != WTERM_SUCCESS) {
        fprintf(stderr, "Failed to get hotspot list\n");
        return -1;
    }

    printf("\n=== Configured Hotspots ===\n\n");

    if (list.count == 0) {
        printf("No hotspots configured\n");
        return 0;
    }

    for (int i = 0; i < list.count; i++) {
        printf("  • %s\n", list.hotspots[i].name);
        printf("    SSID: %s\n", list.hotspots[i].ssid);
        printf("    Security: %s\n",
               list.hotspots[i].security_type == WIFI_SECURITY_NONE ? "Open" : "WPA2-PSK");
        printf("    Band: %s\n", list.hotspots[i].is_5ghz ? "5GHz" : "2.4GHz");
        printf("\n");
    }

    return 0;
}

// Show hotspot status
int hotspot_show_status(void) {
    char name[MAX_STR_SSID];
    if (!hotspot_select(NULL, name, sizeof(name))) {
        fprintf(stderr, "No hotspot selected\n");
        return -1;
    }

    hotspot_status_t status;
    if (hotspot_get_status(name, &status) != WTERM_SUCCESS) {
        fprintf(stderr, "Failed to get status\n");
        return -1;
    }

    printf("\n=== Hotspot Status ===\n\n");
    printf("  Name: %s\n", status.config.name);
    printf("  SSID: %s\n", status.config.ssid);
    printf("  State: %s\n", status.state == HOTSPOT_STATE_ACTIVE ? "Running" : "Stopped");
    printf("  Security: %s\n",
           status.config.security_type == WIFI_SECURITY_NONE ? "Open" : "WPA2-PSK");
    printf("  Interface: %s\n", status.config.wifi_interface);
    printf("\n");

    return 0;
}

// Helper to check if we need root for an operation
// If not root, prompts to restart menu with sudo
static bool check_root_for_operation(const char* operation, int argc, char *argv[]) {
    if (is_root()) {
        return true;
    }

    printf("\n⚠️  %s requires root privileges\n", operation);
    printf("Restart menu with sudo? [y/N]: ");
    fflush(stdout);

    char response[10];
    if (fgets(response, sizeof(response), stdin) == NULL) {
        return false;
    }

    if (response[0] != 'y' && response[0] != 'Y') {
        printf("Operation cancelled\n");
        return false;
    }

    // User wants sudo - restart entire menu with elevation
    printf("\n🔐 Elevating to root...\n\n");

    // Build sudo command to restart menu
    char **sudo_args = malloc(sizeof(char*) * (argc + 2));
    if (!sudo_args) {
        fprintf(stderr, "Memory allocation failed\n");
        return false;
    }

    sudo_args[0] = "sudo";
    for (int i = 0; i < argc; i++) {
        sudo_args[i + 1] = argv[i];
    }
    sudo_args[argc + 1] = NULL;

    // Execute sudo (replaces current process)
    execvp("sudo", sudo_args);

    // If we get here, exec failed
    fprintf(stderr, "Failed to execute sudo: %s\n", strerror(errno));
    free(sudo_args);
    return false;
}

// Interactive main menu
int hotspot_interactive_menu(int argc, char *argv[]) {
    // Don't force elevation at start - let individual operations check as needed
    // This allows read-only operations (list, status) without root privileges

    // Initialize hotspot manager (doesn't require root)
    if (hotspot_manager_init() != WTERM_SUCCESS) {
        fprintf(stderr, "Failed to initialize hotspot manager\n");
        return 1;
    }

    while (true) {
        printf("\n=== Hotspot Management ===\n\n");
        printf("  1. Create new hotspot\n");
        printf("  2. Start hotspot\n");
        printf("  3. Stop hotspot\n");
        printf("  4. Delete hotspot\n");
        printf("  5. List hotspots\n");
        printf("  6. Show status\n");
        printf("  7. Exit\n\n");
        printf("Select option [1-7]: ");
        fflush(stdout);

        int choice;
        if (scanf("%d", &choice) != 1) {
            fprintf(stderr, "Invalid input\n");
            while (getchar() != '\n'); // Clear input buffer
            continue;
        }
        getchar(); // Consume newline

        switch (choice) {
            case 1:
                if (check_root_for_operation("Creating hotspot", argc, argv)) {
                    hotspot_create_wizard();
                }
                break;
            case 2:
                if (check_root_for_operation("Starting hotspot", argc, argv)) {
                    hotspot_start_interactive();
                }
                break;
            case 3:
                if (check_root_for_operation("Stopping hotspot", argc, argv)) {
                    hotspot_stop_interactive();
                }
                break;
            case 4:
                if (check_root_for_operation("Deleting hotspot", argc, argv)) {
                    hotspot_delete_interactive();
                }
                break;
            case 5:
                hotspot_list_all();
                break;
            case 6:
                hotspot_show_status();
                break;
            case 7:
                printf("Exiting...\n");
                hotspot_manager_cleanup();
                return 0;
            default:
                fprintf(stderr, "Invalid option\n");
        }
    }

    return 0;
}
