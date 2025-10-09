#ifndef WTERM_TUI_INTERFACE_H
#define WTERM_TUI_INTERFACE_H

/**
 * @file tui_interface.h
 * @brief Public API for wterm TUI (Terminal User Interface)
 *
 * Provides a lazygit-style 3-panel interface for network selection
 * with vim-style keyboard navigation.
 */

#include "common.h"
#include <stdbool.h>

/**
 * @brief Check if termbox2 TUI is available
 * @return true if TUI can be initialized, false otherwise
 */
bool tui_is_available(void);

/**
 * @brief Initialize TUI system
 * @return WTERM_SUCCESS on success, error code otherwise
 */
wterm_result_t tui_init(void);

/**
 * @brief Cleanup TUI system
 */
void tui_shutdown(void);

/**
 * @brief Show network selection interface with 3-panel layout
 *
 * Displays:
 * - Panel 1: Saved networks
 * - Panel 2: Available networks (scanned)
 * - Panel 3: Keybindings help
 *
 * Navigation:
 * - j/↓: Move down | k/↑: Move up
 * - g: Jump to top | G: Jump to bottom
 * - Tab/h/l: Switch panels
 * - Enter: Select network
 * - r: Rescan
 * - ?: Help
 * - q/Esc: Quit
 *
 * @param networks List of scanned networks
 * @param selected_ssid Output buffer for selected SSID
 * @param buffer_size Size of output buffer
 * @return true if network selected, false if user quit
 *
 * Special return values in selected_ssid:
 * - "RESCAN" - User requested rescan
 * - "HOTSPOT" - User wants hotspot manager
 */
bool tui_select_network(const network_list_t *networks,
                        char *selected_ssid,
                        size_t buffer_size);

/**
 * @brief Get password input for secured network
 *
 * Shows a centered modal dialog with masked password input.
 *
 * @param ssid Network SSID
 * @param password_out Output buffer for password
 * @param buffer_size Size of password buffer
 * @return true if password entered, false if cancelled
 */
bool tui_get_password(const char *ssid,
                      char *password_out,
                      size_t buffer_size);

/**
 * @brief Show message to user
 *
 * Displays a centered message box.
 *
 * @param message Message to display
 * @param wait_for_key If true, wait for keypress; otherwise show briefly
 */
void tui_show_message(const char *message, bool wait_for_key);

/**
 * @brief Show loading animation during network scan
 *
 * Displays a centered spinner with message.
 * Non-blocking - must be called in a loop.
 *
 * @param message Loading message to display
 */
void tui_show_loading(const char *message);

/**
 * @brief Hide loading animation
 */
void tui_hide_loading(void);

#endif // WTERM_TUI_INTERFACE_H
