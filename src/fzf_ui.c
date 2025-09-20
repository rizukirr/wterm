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
#include <unistd.h>
#include <sys/wait.h>

bool fzf_is_available(void) {
    // Check if fzf command is available
    int result = system("command -v fzf >/dev/null 2>&1");
    return (result == 0);
}

// Removed redundant wrapper function - use fzf_select_network_proper directly

bool fzf_get_password(const char* ssid, char* password_out, size_t buffer_size) {
    if (!ssid || !password_out || buffer_size == 0) {
        return false;
    }

    printf("Connect to: %s\n", ssid);
    printf("Enter password: ");
    fflush(stdout);

    // Use getpass for hidden password input, fallback to fgets
    char* password = getpass("");
    if (password && strlen(password) > 0) {
        safe_string_copy(password_out, password, buffer_size);

        // Clear password from memory
        memset(password, 0, strlen(password));
        return true;
    }

    return false;
}

void fzf_show_message(const char* message) {
    if (message) {
        printf("%s\n", message);
    }
}

// Static variables for animation state
static int spinner_state = 0;
static bool animation_running = false;

// Show loading animation during scan
void show_loading_animation(const char* message) {
    const char* spinner[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
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
bool fzf_select_network_proper(const network_list_t* networks, char* selected_ssid, size_t buffer_size) {
    if (!networks || !selected_ssid || buffer_size == 0) {
        return false;
    }

    if (networks->count == 0) {
        printf("No WiFi networks found.\n");
        return false;
    }

    // Create temporary file with network list
    char temp_file[] = "/tmp/wterm_networks_XXXXXX";
    int fd = mkstemp(temp_file);
    if (fd == -1) {
        perror("Failed to create temporary file");
        return false;
    }

    FILE* temp = fdopen(fd, "w");
    if (!temp) {
        close(fd);
        unlink(temp_file);
        return false;
    }

    // Add rescan option as first item
    fprintf(temp, "🔄 Rescan networks\n");

    // Write networks to temp file
    for (int i = 0; i < networks->count; i++) {
        fprintf(temp, "%s\n", networks->networks[i].ssid);
    }
    fclose(temp);

    // Build fzf command with header
    char fzf_command[512];
    snprintf(fzf_command, sizeof(fzf_command),
             "fzf --border --prompt='Select WiFi: ' --height=12 --reverse "
             "--header='↑↓ Navigate | Enter: Select | Type: Search | q/Esc: Quit' < %s",
             temp_file);

    // Run fzf and get selection
    FILE* fzf_output = popen(fzf_command, "r");
    bool success = false;

    if (fzf_output) {
        char selection[256] = {0};
        if (fgets(selection, sizeof(selection), fzf_output)) {
            // Remove trailing newline
            selection[strcspn(selection, "\n")] = '\0';

            if (strlen(selection) > 0) {
                // Check if user selected rescan option
                if (strcmp(selection, "🔄 Rescan networks") == 0) {
                    safe_string_copy(selected_ssid, "RESCAN", buffer_size);
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