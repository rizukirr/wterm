/**
 * @file hotspot_ui.c
 * @brief Interactive hotspot management UI implementation
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "hotspot_ui.h"
#include "../../include/wterm/common.h"
#include "../utils/safe_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>  // For explicit_bzero
#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <errno.h>

#define SCRIPT_PATH "./scripts/hotspot_nm.sh"
#define MAX_BUFFER 1024
#define MAX_COMMAND 2048
#define HOTSPOT_PASSWORD_MAX 64
#define HOTSPOT_INTERFACE_MAX 32
#define HOTSPOT_BAND_MAX 16
#define HOTSPOT_ACTION_MAX 64

// Helper to check if running as root
static bool is_root(void) {
    return (geteuid() == 0);
}

// Check and ensure root privileges for hotspot operations
static void ensure_root_privileges(int argc, char *argv[]) {
    if (is_root()) {
        return;  // Already root, nothing to do
    }

    // Check if sudo is available
    if (!safe_command_exists("sudo")) {
        fprintf(stderr, "ERROR: Hotspot operations require root privileges.\n");
        fprintf(stderr, "Please run as root or install sudo.\n");
        exit(1);
    }

    // Build sudo command arguments
    char **sudo_args = malloc((argc + 2) * sizeof(char *));
    if (!sudo_args) {
        fprintf(stderr, "ERROR: Failed to allocate memory\n");
        exit(1);
    }

    sudo_args[0] = "sudo";
    for (int i = 0; i < argc; i++) {
        sudo_args[i + 1] = argv[i];
    }
    sudo_args[argc + 1] = NULL;

    printf("Hotspot operations require root privileges.\n");
    printf("Re-executing with sudo...\n\n");

    // Execute with sudo
    execvp("sudo", sudo_args);

    // If we get here, exec failed
    fprintf(stderr, "ERROR: Failed to execute with sudo: %s\n", strerror(errno));
    free(sudo_args);
    exit(1);
}

// Helper function to run script command and capture output
static bool run_script_command(const char* args, char* output, size_t output_size) {
    char command[MAX_COMMAND];
    snprintf(command, sizeof(command), "%s %s 2>&1", SCRIPT_PATH, args);

    FILE* pipe = popen(command, "r");
    if (!pipe) {
        return false;
    }

    if (output && output_size > 0) {
        size_t total_read = 0;
        while (fgets(output + total_read, output_size - total_read, pipe) != NULL) {
            total_read = strlen(output);
            if (total_read >= output_size - 1) break;
        }
    }

    int result = pclose(pipe);
    return (result == 0);
}

// Helper function to get fzf selection from script output
static bool get_fzf_selection(const char* script_args, const char* prompt,
                             const char* header, char* selected, size_t selected_size) {
    char command[MAX_COMMAND];

    // Build command: script output | fzf
    snprintf(command, sizeof(command),
             "%s %s 2>/dev/null | fzf --border=rounded --prompt='%s' "
             "--header='%s' --height=40%% --reverse --ansi",
             SCRIPT_PATH, script_args, prompt, header);

    FILE* pipe = popen(command, "r");
    if (!pipe) {
        return false;
    }

    bool got_selection = false;
    if (fgets(selected, selected_size, pipe) != NULL) {
        // Remove newline
        selected[strcspn(selected, "\n")] = 0;
        got_selection = (strlen(selected) > 0);
    }

    pclose(pipe);
    return got_selection;
}

// Helper to get user confirmation
static bool get_confirmation(const char* message) {
    printf("%s (y/N): ", message);
    fflush(stdout);

    char response[10];
    if (fgets(response, sizeof(response), stdin) == NULL) {
        return false;
    }

    return (response[0] == 'y' || response[0] == 'Y');
}

// Helper to get password input (hidden)
static bool get_password_input(const char* prompt, char* password, size_t size) {
    struct termios old_term, new_term;

    // Get current terminal settings
    if (tcgetattr(STDIN_FILENO, &old_term) == -1) {
        return false;  // Can't get terminal state
    }

    // Disable echo
    new_term = old_term;
    new_term.c_lflag &= ~ECHO;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &new_term) == -1) {
        return false;  // Can't disable echo
    }

    printf("%s: ", prompt);
    fflush(stdout);

    bool success = (fgets(password, size, stdin) != NULL);
    if (success) {
        password[strcspn(password, "\n")] = 0;  // Remove newline
    }

    // Re-enable echo (always attempt, even on error)
    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    printf("\n");

    return success && (strlen(password) > 0);
}

bool hotspot_select(const char* filter, char* selected_name, size_t buffer_size) {
    char selection[MAX_BUFFER];
    (void)filter;  // TODO: Implement filtering by status

    if (!get_fzf_selection("interactive list-hotspots", "Select Hotspot > ",
                          "Arrow keys to navigate | Enter to select | ESC to cancel",
                          selection, sizeof(selection))) {
        return false;
    }

    // Parse format: name\tstatus\tdetails
    char* tab_pos = strchr(selection, '\t');
    if (tab_pos) {
        size_t name_len = tab_pos - selection;
        if (name_len < buffer_size) {
            strncpy(selected_name, selection, name_len);
            selected_name[name_len] = '\0';
            return true;
        }
    }

    return false;
}

int hotspot_create_wizard(void) {
    char name[MAX_STR_SSID];
    char password[HOTSPOT_PASSWORD_MAX];
    char band[HOTSPOT_BAND_MAX];
    char interface[HOTSPOT_INTERFACE_MAX];

    printf("\n=== Create New Hotspot ===\n\n");

    // Step 1: Get hotspot name
    printf("Hotspot name: ");
    fflush(stdout);
    if (fgets(name, sizeof(name), stdin) == NULL || strlen(name) <= 1) {
        printf("Cancelled.\n");
        return 1;
    }
    name[strcspn(name, "\n")] = 0;

    // Step 2: Security type selection
    char security_selection[MAX_BUFFER];
    if (!get_fzf_selection("interactive list-security", "Security > ",
                          "Select security type", security_selection,
                          sizeof(security_selection))) {
        printf("Cancelled.\n");
        return 1;
    }

    // Parse selection
    bool is_open = (strncmp(security_selection, "open", 4) == 0);

    // Step 3: Get password if secured
    if (!is_open) {
        if (!get_password_input("Password (8-63 characters)", password, sizeof(password))) {
            printf("Cancelled.\n");
            explicit_bzero(password, sizeof(password));
            return 1;
        }

        if (strlen(password) < 8 || strlen(password) > 63) {
            printf("ERROR: Password must be 8-63 characters\n");
            explicit_bzero(password, sizeof(password));
            return -1;
        }

        // Confirm password
        char password_confirm[HOTSPOT_PASSWORD_MAX];
        if (!get_password_input("Confirm password", password_confirm, sizeof(password_confirm))) {
            printf("Cancelled.\n");
            return 1;
        }

        if (strcmp(password, password_confirm) != 0) {
            printf("ERROR: Passwords do not match\n");
            explicit_bzero(password, sizeof(password));
            explicit_bzero(password_confirm, sizeof(password_confirm));
            return -1;
        }

        // Clear confirmation password immediately after validation
        explicit_bzero(password_confirm, sizeof(password_confirm));
    }

    // Step 4: Frequency band selection
    char band_selection[MAX_BUFFER];
    if (!get_fzf_selection("interactive list-bands", "Band > ",
                          "Select frequency band", band_selection,
                          sizeof(band_selection))) {
        printf("Cancelled.\n");
        return 1;
    }

    // Extract band code (bg or a)
    size_t band_tab_pos = strcspn(band_selection, "\t");
    strncpy(band, band_selection, band_tab_pos);
    band[band_tab_pos] = '\0';

    // Step 5: Interface selection
    char interface_selection[MAX_BUFFER];
    if (!get_fzf_selection("interactive get-interfaces", "Interface > ",
                          "Select WiFi interface", interface_selection,
                          sizeof(interface_selection))) {
        printf("Cancelled.\n");
        return 1;
    }

    // Extract interface name
    size_t iface_tab_pos = strcspn(interface_selection, "\t");
    strncpy(interface, interface_selection, iface_tab_pos);
    interface[iface_tab_pos] = '\0';

    // Step 6: Confirm
    printf("\n=== Configuration Summary ===\n");
    printf("Name:      %s\n", name);
    printf("Security:  %s\n", is_open ? "Open" : "WPA2-PSK");
    printf("Band:      %s\n", strstr(band, "bg") ? "2.4GHz" : "5GHz");
    printf("Interface: %s\n", interface);
    printf("\n");

    if (!get_confirmation("Create this hotspot?")) {
        printf("Cancelled.\n");
        return 1;
    }

    // Step 7: Create hotspot
    char create_command[MAX_BUFFER];
    if (is_open) {
        snprintf(create_command, sizeof(create_command),
                 "create %s --open --band %s --interface %s",
                 name, band, interface);
    } else {
        snprintf(create_command, sizeof(create_command),
                 "create %s '%s' --band %s --interface %s",
                 name, password, band, interface);
    }

    // Clear password from memory immediately after use
    explicit_bzero(password, sizeof(password));

    char output[MAX_BUFFER] = {0};
    if (run_script_command(create_command, output, sizeof(output))) {
        printf("\n%s\n", output);
        return 0;
    } else {
        printf("\nERROR: Failed to create hotspot\n%s\n", output);
        return -1;
    }
}

// Helper function for common hotspot actions (start/stop/delete)
static int run_hotspot_action(const char* action, const char* filter) {
    char hotspot_name[MAX_STR_SSID];

    if (!hotspot_select(filter, hotspot_name, sizeof(hotspot_name))) {
        printf("Cancelled.\n");
        return 1;
    }

    char command[MAX_BUFFER];
    snprintf(command, sizeof(command), "%s %s", action, hotspot_name);

    char output[MAX_BUFFER] = {0};
    if (run_script_command(command, output, sizeof(output))) {
        printf("\n%s\n", output);
        return 0;
    } else {
        printf("\nERROR: Failed to %s hotspot\n%s\n", action, output);
        return -1;
    }
}

int hotspot_start_interactive(void) {
    return run_hotspot_action("start", "stopped");
}

int hotspot_stop_interactive(void) {
    return run_hotspot_action("stop", "running");
}

int hotspot_delete_interactive(void) {
    char hotspot_name[MAX_STR_SSID];

    if (!hotspot_select(NULL, hotspot_name, sizeof(hotspot_name))) {
        printf("Cancelled.\n");
        return 1;
    }

    if (!get_confirmation("Are you sure you want to delete this hotspot?")) {
        printf("Cancelled.\n");
        return 1;
    }

    // Build command directly since we already have the name
    char command[MAX_BUFFER];
    snprintf(command, sizeof(command), "delete %s", hotspot_name);

    char output[MAX_BUFFER] = {0};
    if (run_script_command(command, output, sizeof(output))) {
        printf("\n%s\n", output);
        return 0;
    } else {
        printf("\nERROR: Failed to delete hotspot\n%s\n", output);
        return -1;
    }
}

int hotspot_list_all(void) {
    char output[MAX_BUFFER] = {0};
    if (run_script_command("list", output, sizeof(output))) {
        printf("\n%s\n", output);
        return 0;
    } else {
        printf("\nERROR: Failed to list hotspots\n%s\n", output);
        return -1;
    }
}

int hotspot_show_status(void) {
    char output[MAX_BUFFER] = {0};
    if (run_script_command("status", output, sizeof(output))) {
        printf("\n%s\n", output);
        return 0;
    } else {
        printf("\nERROR: Failed to show status\n%s\n", output);
        return -1;
    }
}

int hotspot_interactive_menu(int argc, char *argv[]) {
    // Ensure we have root privileges for hotspot operations
    ensure_root_privileges(argc, argv);

    while (1) {
        char selection[MAX_BUFFER];

        if (!get_fzf_selection("interactive list-actions", "Hotspot Manager > ",
                              "Select an action (ESC to exit)",
                              selection, sizeof(selection))) {
            // User cancelled (ESC)
            return 0;
        }

        // Parse action from format: action\tdescription
        char action[HOTSPOT_ACTION_MAX];
        size_t action_tab_pos = strcspn(selection, "\t");
        strncpy(action, selection, action_tab_pos);
        action[action_tab_pos] = '\0';

        int result = 0;
        if (strcmp(action, "create") == 0) {
            result = hotspot_create_wizard();
        } else if (strcmp(action, "start") == 0) {
            result = hotspot_start_interactive();
        } else if (strcmp(action, "stop") == 0) {
            result = hotspot_stop_interactive();
        } else if (strcmp(action, "restart") == 0) {
            result = run_hotspot_action("restart", NULL);
        } else if (strcmp(action, "delete") == 0) {
            result = hotspot_delete_interactive();
        } else if (strcmp(action, "list") == 0) {
            result = hotspot_list_all();
        } else if (strcmp(action, "status") == 0) {
            result = hotspot_show_status();
        } else if (strcmp(action, "exit") == 0) {
            return 0;
        }

        // Pause before returning to menu
        if (result != 1) {  // Don't pause if user cancelled
            // Clear input buffer
            int c;
            while ((c = getchar()) != '\n' && c != EOF);

            printf("\nPress Enter to continue...");
            getchar();
        }
    }

    return 0;
}
