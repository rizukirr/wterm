#ifndef HOTSPOT_UI_H
#define HOTSPOT_UI_H

/**
 * @file hotspot_ui.h
 * @brief Interactive hotspot management UI using fzf
 */

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Run interactive hotspot management menu
 *
 * Displays main menu with options to create, start, stop, delete hotspots.
 * Uses text-based selection interface for menu navigation.
 * Individual operations check for root privileges as needed.
 *
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 * @return 0 on success, non-zero on error
 */
int hotspot_interactive_menu(int argc, char *argv[]);

/**
 * @brief Select a hotspot from available hotspots using fzf
 *
 * @param action Filter: "running" for active hotspots, "stopped" for inactive, NULL for all
 * @param selected_name Output buffer for selected hotspot name
 * @param buffer_size Size of output buffer
 * @return true if hotspot selected, false if cancelled or error
 */
bool hotspot_select(const char* action, char* selected_name, size_t buffer_size);

/**
 * @brief Interactive wizard to create new hotspot
 *
 * Guides user through:
 * - Hotspot name
 * - Security type (secured/open)
 * - Password (if secured)
 * - Frequency band (2.4GHz/5GHz)
 * - Interface selection
 * - Auto-start option
 *
 * @return 0 on success, 1 if cancelled, negative on error
 */
int hotspot_create_wizard(void);

/**
 * @brief Start an existing hotspot
 *
 * Shows list of stopped hotspots and starts the selected one.
 *
 * @return 0 on success, non-zero on error
 */
int hotspot_start_interactive(void);

/**
 * @brief Stop a running hotspot
 *
 * Shows list of running hotspots and stops the selected one.
 *
 * @return 0 on success, non-zero on error
 */
int hotspot_stop_interactive(void);

/**
 * @brief Delete a hotspot configuration
 *
 * Shows list of hotspots and deletes the selected one after confirmation.
 *
 * @return 0 on success, non-zero on error
 */
int hotspot_delete_interactive(void);

/**
 * @brief List all hotspots with details
 *
 * Shows formatted list of all hotspot configurations with status.
 *
 * @return 0 on success, non-zero on error
 */
int hotspot_list_all(void);

/**
 * @brief Show detailed status of hotspots
 *
 * Displays status of all hotspots or a specific hotspot.
 *
 * @return 0 on success, non-zero on error
 */
int hotspot_show_status(void);

#endif // HOTSPOT_UI_H
