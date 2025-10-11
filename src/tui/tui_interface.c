/**
 * @file tui_interface.c
 * @brief Production TUI implementation for wterm
 */

#define TB_IMPL
#include "../../include/external/termbox2.h"
#include "../../include/wterm/tui_interface.h"
#include "../../include/wterm/common.h"
#include "../core/connection.h"
#include "../core/hotspot_manager.h"
#include "../utils/string_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

// Thread data for background connection
typedef struct {
    char ssid[MAX_STR_SSID];
    char password[MAX_STR_PASSWORD];
    bool is_open;
    bool is_saved;
    connection_result_t result;
    volatile sig_atomic_t is_done;  // Thread completion flag
} connection_thread_data_t;

// Thread function for background connection
static void* connection_thread_func(void *arg) {
    connection_thread_data_t *data = (connection_thread_data_t*)arg;

    if (data->is_open) {
        data->result = connect_to_open_network(data->ssid);
    } else if (data->is_saved) {
        data->result = connect_to_secured_network(data->ssid, "");
    } else {
        data->result = connect_to_secured_network(data->ssid, data->password);
        // Clear password from memory after use
        memset(data->password, 0, sizeof(data->password));
    }

    // Signal that thread is done
    data->is_done = 1;

    return NULL;
}

// Panel structure for internal use
typedef struct {
    int x, y;
    int width, height;
    const char *title;
    bool is_active;
    int selected;
    int scroll_offset;
    int item_count;
} tui_panel_t;

// Static TUI state
static bool tui_initialized = false;
static connection_status_t current_connection_status = {0};
static hotspot_list_t current_hotspots = {0};

// ============================================================================
// Helper Functions
// ============================================================================

static void draw_hline(int x, int y, int width, uint32_t ch, uintptr_t fg, uintptr_t bg) {
    for (int i = 0; i < width; i++) {
        tb_set_cell(x + i, y, ch, fg, bg);
    }
}

static void draw_vline(int x, int y, int height, uint32_t ch, uintptr_t fg, uintptr_t bg) {
    for (int i = 0; i < height; i++) {
        tb_set_cell(x, y + i, ch, fg, bg);
    }
}

static void draw_panel_border(tui_panel_t *panel) {
    uintptr_t fg = panel->is_active ? TB_CYAN | TB_BOLD : TB_WHITE;
    uintptr_t bg = TB_DEFAULT;

    // Top border
    tb_set_cell(panel->x, panel->y, 0x250C, fg, bg);  // ┌
    draw_hline(panel->x + 1, panel->y, panel->width - 2, 0x2500, fg, bg);  // ─
    tb_set_cell(panel->x + panel->width - 1, panel->y, 0x2510, fg, bg);  // ┐

    // Title
    int title_len = strlen(panel->title);
    int title_x = panel->x + 2;
    tb_printf(title_x, panel->y, fg, bg, " %s ", panel->title);

    // Side borders
    draw_vline(panel->x, panel->y + 1, panel->height - 2, 0x2502, fg, bg);  // │
    draw_vline(panel->x + panel->width - 1, panel->y + 1, panel->height - 2, 0x2502, fg, bg);  // │

    // Bottom border
    tb_set_cell(panel->x, panel->y + panel->height - 1, 0x2514, fg, bg);  // └
    draw_hline(panel->x + 1, panel->y + panel->height - 1, panel->width - 2, 0x2500, fg, bg);  // ─
    tb_set_cell(panel->x + panel->width - 1, panel->y + panel->height - 1, 0x2518, fg, bg);  // ┘
}

static void clear_panel_content(tui_panel_t *panel) {
    for (int y = 1; y < panel->height - 1; y++) {
        for (int x = 1; x < panel->width - 1; x++) {
            tb_set_cell(panel->x + x, panel->y + y, ' ', TB_DEFAULT, TB_DEFAULT);
        }
    }
}

static void render_signal_bar(int x, int y, int signal, uintptr_t fg, uintptr_t bg) {
    int bars = (signal / 25);  // 0-4 bars
    if (bars > 4) bars = 4;

    tb_set_cell(x++, y, '[', fg, bg);
    tb_set_cell(x++, y, (bars >= 1) ? 0x2588 : ' ', fg, bg);  // █
    tb_set_cell(x++, y, (bars >= 2) ? 0x2588 : ' ', fg, bg);
    tb_set_cell(x++, y, (bars >= 3) ? 0x2588 : ' ', fg, bg);
    tb_set_cell(x++, y, (bars >= 4) ? 0x2588 : ' ', fg, bg);
    tb_set_cell(x++, y, ']', fg, bg);
}

static void move_selection(tui_panel_t *panel, int direction) {
    if (panel->item_count == 0) return;

    int new_selected = panel->selected + direction;

    // Clamp to valid range
    if (new_selected < 0) new_selected = 0;
    if (new_selected >= panel->item_count) new_selected = panel->item_count - 1;

    panel->selected = new_selected;

    // Update scroll offset
    int visible_lines = panel->height - 2;
    if (panel->selected < panel->scroll_offset) {
        panel->scroll_offset = panel->selected;
    } else if (panel->selected >= panel->scroll_offset + visible_lines) {
        panel->scroll_offset = panel->selected - visible_lines + 1;
    }
}

// ============================================================================
// Rendering Functions
// ============================================================================

static void render_saved_networks_list(tui_panel_t *panel, const network_list_t *networks) {
    int visible_lines = panel->height - 2;
    int start = panel->scroll_offset;
    int end = start + visible_lines;

    // Build list of saved networks (only show networks that are actually saved)
    int saved_indices[MAX_NETWORKS];
    int saved_count = 0;

    for (int i = 0; i < networks->count && saved_count < MAX_NETWORKS; i++) {
        if (is_saved_connection(networks->networks[i].ssid)) {
            saved_indices[saved_count++] = i;
        }
    }

    if (end > saved_count) end = saved_count;

    // Render saved networks
    for (int list_idx = start; list_idx < end; list_idx++) {
        int network_idx = saved_indices[list_idx];
        int y = panel->y + 1 + (list_idx - start);
        int x = panel->x + 2;

        uintptr_t fg = TB_WHITE;
        uintptr_t bg = TB_DEFAULT;

        if (list_idx == panel->selected && panel->is_active) {
            fg = TB_BLACK;
            bg = TB_WHITE;
        }

        // Check if connected to this network
        bool is_connected = current_connection_status.is_connected &&
                           strcmp(current_connection_status.connected_ssid, networks->networks[network_idx].ssid) == 0;

        // Show ✓ if connected, otherwise just the SSID
        if (is_connected) {
            uintptr_t indicator_fg = (list_idx == panel->selected && panel->is_active) ? fg : TB_GREEN | TB_BOLD;
            tb_printf(x, y, indicator_fg, bg, "✓ ");
            tb_printf(x + 2, y, fg, bg, "%-30s", networks->networks[network_idx].ssid);
        } else {
            tb_printf(x, y, fg, bg, "  %-30s", networks->networks[network_idx].ssid);
        }
    }

    // Scroll indicator
    if (saved_count > visible_lines && saved_count > 0) {
        int indicator_y = panel->y + 1;
        int progress = (panel->selected * (visible_lines - 1)) / (saved_count - 1);
        tb_set_cell(panel->x + panel->width - 2, indicator_y + progress, 0x2588, TB_CYAN, TB_DEFAULT);
    }
}

static void render_available_networks(tui_panel_t *panel, const network_list_t *networks) {
    int visible_lines = panel->height - 2;
    int start = panel->scroll_offset;
    int end = start + visible_lines;
    if (end > networks->count) end = networks->count;

    for (int i = start; i < end; i++) {
        int y = panel->y + 1 + (i - start);
        int x = panel->x + 2;

        uintptr_t fg = TB_WHITE;
        uintptr_t bg = TB_DEFAULT;

        if (i == panel->selected && panel->is_active) {
            fg = TB_BLACK;
            bg = TB_WHITE;
        }

        // Check connection status
        bool is_connected = current_connection_status.is_connected &&
                           strcmp(current_connection_status.connected_ssid, networks->networks[i].ssid) == 0;

        // Show ✓ only if connected (no saved indicator)
        const char *indicator = " ";
        uintptr_t indicator_fg = fg;

        if (is_connected) {
            indicator = "✓";  // Connected (U+2713)
            indicator_fg = (i == panel->selected && panel->is_active) ? fg : TB_GREEN | TB_BOLD;
        }

        // Selection arrow (only when active panel and selected)
        const char *arrow = (i == panel->selected && panel->is_active) ? "→" : " ";

        // Draw indicator and arrow
        tb_printf(x, y, indicator_fg, bg, "%s", indicator);
        tb_printf(x + 2, y, fg, bg, "%s", arrow);

        // SSID (left-aligned, 20 chars)
        char ssid_buf[32];
        snprintf(ssid_buf, sizeof(ssid_buf), "%-18s", networks->networks[i].ssid);
        tb_printf(x + 4, y, fg, bg, "%s", ssid_buf);

        // Signal bar (parse signal string to int)
        int signal_strength = atoi(networks->networks[i].signal);
        render_signal_bar(x + 23, y, signal_strength, fg, bg);

        // Signal percentage and security
        tb_printf(x + 30, y, fg, bg, " %3d%%  %-8s",
                  signal_strength,
                  networks->networks[i].security);
    }

    // Scroll indicator
    if (networks->count > visible_lines && networks->count > 0) {
        int indicator_y = panel->y + 1;
        int progress = (panel->selected * (visible_lines - 1)) / (networks->count - 1);
        tb_set_cell(panel->x + panel->width - 2, indicator_y + progress, 0x2588, TB_CYAN, TB_DEFAULT);
    }
}

static void draw_help_modal(void) {
    int width = tb_width();
    int height = tb_height();

    int modal_width = 60;
    int modal_height = 23;
    int modal_x = (width - modal_width) / 2;
    int modal_y = (height - modal_height) / 2;

    // Background (same as panels - default/black)
    uintptr_t bg = TB_DEFAULT;
    uintptr_t fg = TB_WHITE;
    for (int y = 0; y < modal_height; y++) {
        for (int x = 0; x < modal_width; x++) {
            tb_set_cell(modal_x + x, modal_y + y, ' ', fg, bg);
        }
    }

    // Border (same style as panels - cyan/white)
    uintptr_t border_fg = TB_CYAN | TB_BOLD;

    // Top border
    tb_set_cell(modal_x, modal_y, 0x250C, border_fg, bg);  // ┌
    draw_hline(modal_x + 1, modal_y, modal_width - 2, 0x2500, border_fg, bg);  // ─
    tb_set_cell(modal_x + modal_width - 1, modal_y, 0x2510, border_fg, bg);  // ┐

    // Title in border
    tb_printf(modal_x + (modal_width - 6) / 2, modal_y, TB_YELLOW | TB_BOLD, bg, " HELP ");

    // Side borders
    draw_vline(modal_x, modal_y + 1, modal_height - 2, 0x2502, border_fg, bg);  // │
    draw_vline(modal_x + modal_width - 1, modal_y + 1, modal_height - 2, 0x2502, border_fg, bg);  // │

    // Bottom border
    tb_set_cell(modal_x, modal_y + modal_height - 1, 0x2514, border_fg, bg);  // └
    draw_hline(modal_x + 1, modal_y + modal_height - 1, modal_width - 2, 0x2500, border_fg, bg);  // ─
    tb_set_cell(modal_x + modal_width - 1, modal_y + modal_height - 1, 0x2518, border_fg, bg);  // ┘

    // Content
    int cx = modal_x + 3;
    int cy = modal_y + 2;

    tb_printf(cx, cy++, TB_CYAN | TB_BOLD, bg, "Navigation:");
    tb_printf(cx, cy++, fg, bg, "  j / ↓         Move down in current panel");
    tb_printf(cx, cy++, fg, bg, "  k / ↑         Move up in current panel");
    tb_printf(cx, cy++, fg, bg, "  g             Jump to top of list");
    tb_printf(cx, cy++, fg, bg, "  G             Jump to bottom of list");
    cy++;

    tb_printf(cx, cy++, TB_CYAN | TB_BOLD, bg, "Panel Switching:");
    tb_printf(cx, cy++, fg, bg, "  Tab / h / l   Switch between panels");
    cy++;

    tb_printf(cx, cy++, TB_CYAN | TB_BOLD, bg, "Network Panel Actions:");
    tb_printf(cx, cy++, fg, bg, "  c / Enter     Connect to selected network");
    tb_printf(cx, cy++, fg, bg, "  d             Disconnect from network");
    tb_printf(cx, cy++, fg, bg, "  r             Rescan networks");
    cy++;

    tb_printf(cx, cy++, TB_CYAN | TB_BOLD, bg, "Hotspot Panel Actions:");
    tb_printf(cx, cy++, fg, bg, "  Enter         Start/Stop selected hotspot");
    tb_printf(cx, cy++, fg, bg, "  n             Create new hotspot");
    tb_printf(cx, cy++, fg, bg, "  d             Delete selected hotspot");
    tb_printf(cx, cy++, fg, bg, "  r             Refresh hotspot list");
    cy++;

    tb_printf(cx, cy++, TB_CYAN | TB_BOLD, bg, "General:");
    tb_printf(cx, cy++, fg, bg, "  ?             Toggle this help");
    tb_printf(cx, cy++, fg, bg, "  q             Quit application");

    tb_printf(modal_x + (modal_width - 30) / 2, modal_y + modal_height - 2,
              TB_YELLOW | TB_BOLD, bg, "Press any key to close...");
}

// ============================================================================
// Modal Dialog Functions
// ============================================================================

/**
 * @brief Draw a centered modal box with title
 * @param width Modal width
 * @param height Modal height
 * @param title Modal title (NULL for no title)
 * @return Starting coordinates {x, y}
 */
static void draw_modal_box(int width, int height, const char *title, int *out_x, int *out_y) {
    int screen_w = tb_width();
    int screen_h = tb_height();

    int x = (screen_w - width) / 2;
    int y = (screen_h - height) / 2;

    uintptr_t bg = TB_DEFAULT;
    uintptr_t border_fg = TB_CYAN | TB_BOLD;

    // Clear modal area
    for (int dy = 0; dy < height; dy++) {
        for (int dx = 0; dx < width; dx++) {
            tb_set_cell(x + dx, y + dy, ' ', TB_WHITE, bg);
        }
    }

    // Top border
    tb_set_cell(x, y, 0x250C, border_fg, bg);  // ┌
    draw_hline(x + 1, y, width - 2, 0x2500, border_fg, bg);  // ─
    tb_set_cell(x + width - 1, y, 0x2510, border_fg, bg);  // ┐

    // Title
    if (title) {
        int title_len = strlen(title);
        int title_x = x + (width - title_len - 2) / 2;
        tb_printf(title_x, y, TB_YELLOW | TB_BOLD, bg, " %s ", title);
    }

    // Side borders
    draw_vline(x, y + 1, height - 2, 0x2502, border_fg, bg);  // │
    draw_vline(x + width - 1, y + 1, height - 2, 0x2502, border_fg, bg);  // │

    // Bottom border
    tb_set_cell(x, y + height - 1, 0x2514, border_fg, bg);  // └
    draw_hline(x + 1, y + height - 1, width - 2, 0x2500, border_fg, bg);  // ─
    tb_set_cell(x + width - 1, y + height - 1, 0x2518, border_fg, bg);  // ┘

    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
}

/**
 * @brief Show password input modal with textbox
 * @param ssid Network SSID
 * @param password_out Output buffer for password
 * @param max_len Maximum password length
 * @return true if password entered, false if cancelled
 */
static bool draw_password_input_modal(const char *ssid, char *password_out, size_t max_len) {
    char buffer[256] = {0};
    int cursor_pos = 0;
    int modal_width = 60;
    int modal_height = 9;

    while (true) {
        int x, y;
        draw_modal_box(modal_width, modal_height, "Enter Password", &x, &y);

        // SSID
        tb_printf(x + 3, y + 2, TB_WHITE, TB_DEFAULT, "SSID: %s", ssid);

        // Password label
        tb_printf(x + 3, y + 4, TB_WHITE, TB_DEFAULT, "Password:");

        // Textbox border
        int box_x = x + 14;
        int box_y = y + 4;
        int box_width = 40;

        tb_set_cell(box_x - 1, box_y, '[', TB_CYAN, TB_DEFAULT);
        tb_set_cell(box_x + box_width, box_y, ']', TB_CYAN, TB_DEFAULT);

        // Draw textbox background
        for (int i = 0; i < box_width; i++) {
            tb_set_cell(box_x + i, box_y, '_', TB_WHITE, TB_DEFAULT);
        }

        // Draw masked password (●)
        for (int i = 0; i < cursor_pos && i < box_width; i++) {
            tb_set_cell(box_x + i, box_y, 0x2022, TB_WHITE, TB_DEFAULT);  // •
        }

        // Draw cursor
        if (cursor_pos < box_width) {
            tb_set_cell(box_x + cursor_pos, box_y, '_', TB_BLACK, TB_WHITE);  // Reverse colors
        }

        // Character count and hint
        int char_count = cursor_pos;
        uintptr_t count_color = (char_count >= 8 && char_count <= 63) ? TB_GREEN : TB_YELLOW;
        tb_printf(x + 3, y + 6, count_color, TB_DEFAULT, "Length: %d/63 (min 8 chars)", char_count);

        // Instructions
        tb_printf(x + 3, y + modal_height - 2, TB_YELLOW, TB_DEFAULT,
                  "Enter: Confirm  |  Esc: Cancel");

        tb_present();

        // Handle input
        struct tb_event ev;
        tb_poll_event(&ev);

        if (ev.type == TB_EVENT_KEY) {
            if (ev.key == TB_KEY_ENTER) {
                if (cursor_pos >= 8) {  // WPA2 minimum
                    strncpy(password_out, buffer, max_len - 1);
                    password_out[max_len - 1] = '\0';
                    memset(buffer, 0, sizeof(buffer));  // Clear buffer
                    return true;
                }
                // If too short, beep or ignore (stay in loop)
            } else if (ev.key == TB_KEY_ESC) {
                memset(buffer, 0, sizeof(buffer));  // Clear buffer
                return false;
            } else if (ev.key == TB_KEY_BACKSPACE || ev.key == TB_KEY_BACKSPACE2) {
                if (cursor_pos > 0) {
                    cursor_pos--;
                    buffer[cursor_pos] = '\0';
                }
            } else if (ev.ch && cursor_pos < (int)max_len - 1 && cursor_pos < 63) {
                buffer[cursor_pos++] = ev.ch;
                buffer[cursor_pos] = '\0';
            }
        }
    }
}

/**
 * @brief Show Yes/No confirmation modal
 * @param message Confirmation message
 * @param default_yes Default to Yes (true) or No (false)
 * @return true if confirmed, false if cancelled
 */
static bool draw_confirmation_modal(const char *message, bool default_yes) {
    int modal_width = 50;
    int modal_height = 7;

    while (true) {
        int x, y;
        draw_modal_box(modal_width, modal_height, "Confirm", &x, &y);

        // Message (word-wrapped if needed)
        tb_printf(x + 3, y + 2, TB_WHITE, TB_DEFAULT, "%s", message);

        // Prompt
        const char *prompt = default_yes ? "[Y/n]" : "[y/N]";
        tb_printf(x + 3, y + 4, TB_YELLOW | TB_BOLD, TB_DEFAULT, "%s", prompt);

        tb_present();

        struct tb_event ev;
        tb_poll_event(&ev);

        if (ev.type == TB_EVENT_KEY) {
            if (ev.key == TB_KEY_ENTER) {
                return default_yes;
            } else if (ev.key == TB_KEY_ESC) {
                return false;
            } else if (ev.ch == 'y' || ev.ch == 'Y') {
                return true;
            } else if (ev.ch == 'n' || ev.ch == 'N') {
                return false;
            }
        }
    }
}

/**
 * @brief Show message modal with auto-timeout or key press
 * @param message Message to display
 * @param is_error true for error (red), false for info (white)
 * @param timeout_ms Timeout in milliseconds (0 = wait for key)
 */
static void draw_message_modal(const char *message, bool is_error, int timeout_ms) {
    int modal_width = 50;
    int modal_height = 7;
    int x, y;

    draw_modal_box(modal_width, modal_height, is_error ? "Error" : "Info", &x, &y);

    uintptr_t msg_color = is_error ? TB_RED | TB_BOLD : TB_GREEN | TB_BOLD;

    // Message
    tb_printf(x + 3, y + 2, msg_color, TB_DEFAULT, "%s", message);

    // Footer
    if (timeout_ms > 0) {
        tb_printf(x + 3, y + modal_height - 2, TB_YELLOW, TB_DEFAULT,
                  "Auto-closing in %d seconds...", timeout_ms / 1000);
    } else {
        tb_printf(x + 3, y + modal_height - 2, TB_YELLOW, TB_DEFAULT,
                  "Press any key to continue...");
    }

    tb_present();

    if (timeout_ms > 0) {
        // TODO: Implement timeout with tb_peek_event()
        // For now, just wait for key
        struct tb_event ev;
        tb_poll_event(&ev);
    } else {
        struct tb_event ev;
        tb_poll_event(&ev);
    }
}

/**
 * @brief Show progress modal (non-blocking display)
 * @param message Progress message (e.g., "Connecting...")
 */
static void draw_progress_modal(const char *message) {
    int modal_width = 50;
    int modal_height = 7;
    int x, y;

    draw_modal_box(modal_width, modal_height, "Please Wait", &x, &y);

    // Message with spinner (future enhancement)
    tb_printf(x + 3, y + 2, TB_CYAN | TB_BOLD, TB_DEFAULT, "%s", message);

    tb_present();
}

/**
 * @brief Show selection modal (list of options)
 * @param title Modal title
 * @param prompt Prompt text
 * @param options Array of option strings
 * @param option_count Number of options
 * @param default_index Default selected index
 * @return Selected index, or -1 if cancelled
 */
static int draw_selection_modal(const char *title, const char *prompt,
                                 const char **options, int option_count,
                                 int default_index) {
    int selected = default_index;
    int modal_width = 60;
    int modal_height = 10 + option_count;

    while (true) {
        int x, y;
        draw_modal_box(modal_width, modal_height, title, &x, &y);

        // Prompt
        if (prompt) {
            tb_printf(x + 3, y + 2, TB_WHITE, TB_DEFAULT, "%s", prompt);
        }

        // Options list
        for (int i = 0; i < option_count; i++) {
            int opt_y = y + 4 + i;
            uintptr_t fg = TB_WHITE;
            uintptr_t bg = TB_DEFAULT;

            if (i == selected) {
                fg = TB_BLACK;
                bg = TB_WHITE;
            }

            const char *arrow = (i == selected) ? "→" : " ";
            tb_printf(x + 3, opt_y, fg, bg, "%s %d. %-40s", arrow, i + 1, options[i]);
        }

        // Instructions
        tb_printf(x + 3, y + modal_height - 2, TB_YELLOW, TB_DEFAULT,
                  "j/k/↑/↓:Navigate  Enter:Select  Esc:Cancel");

        tb_present();

        // Handle input
        struct tb_event ev;
        tb_poll_event(&ev);

        if (ev.type == TB_EVENT_KEY) {
            if (ev.key == TB_KEY_ENTER) {
                return selected;
            } else if (ev.key == TB_KEY_ESC) {
                return -1;
            } else if (ev.key == TB_KEY_ARROW_DOWN || ev.ch == 'j') {
                selected = (selected + 1) % option_count;
            } else if (ev.key == TB_KEY_ARROW_UP || ev.ch == 'k') {
                selected = (selected - 1 + option_count) % option_count;
            } else if (ev.ch >= '1' && ev.ch <= '9') {
                int num = ev.ch - '0';
                if (num <= option_count) {
                    selected = num - 1;
                }
            }
        }
    }
}

/**
 * @brief Show text input modal
 * @param title Modal title
 * @param prompt Prompt text
 * @param placeholder Placeholder text (hint)
 * @param output_buffer Output buffer
 * @param max_len Maximum length
 * @return true if text entered, false if cancelled
 */
static bool draw_text_input_modal(const char *title, const char *prompt,
                                   const char *placeholder, char *output_buffer,
                                   size_t max_len) {
    char buffer[256] = {0};
    int cursor_pos = 0;
    int modal_width = 60;
    int modal_height = 10;

    while (true) {
        int x, y;
        draw_modal_box(modal_width, modal_height, title, &x, &y);

        // Prompt
        if (prompt) {
            tb_printf(x + 3, y + 2, TB_WHITE, TB_DEFAULT, "%s", prompt);
        }

        // Input label
        tb_printf(x + 3, y + 4, TB_WHITE, TB_DEFAULT, "Input:");

        // Textbox border
        int box_x = x + 10;
        int box_y = y + 4;
        int box_width = 44;

        tb_set_cell(box_x - 1, box_y, '[', TB_CYAN, TB_DEFAULT);
        tb_set_cell(box_x + box_width, box_y, ']', TB_CYAN, TB_DEFAULT);

        // Draw textbox background
        for (int i = 0; i < box_width; i++) {
            char ch = (cursor_pos == 0 && placeholder && i < (int)strlen(placeholder))
                      ? placeholder[i] : '_';
            uintptr_t color = (cursor_pos == 0 && placeholder) ? TB_BLUE : TB_WHITE;
            tb_set_cell(box_x + i, box_y, ch, color, TB_DEFAULT);
        }

        // Draw text
        for (int i = 0; i < cursor_pos && i < box_width; i++) {
            tb_set_cell(box_x + i, box_y, buffer[i], TB_WHITE, TB_DEFAULT);
        }

        // Draw cursor
        if (cursor_pos < box_width) {
            char cursor_char = (cursor_pos < (int)strlen(buffer)) ? buffer[cursor_pos] : '_';
            tb_set_cell(box_x + cursor_pos, box_y, cursor_char, TB_BLACK, TB_WHITE);
        }

        // Character count
        tb_printf(x + 3, y + 6, TB_YELLOW, TB_DEFAULT, "Length: %d/%zu", cursor_pos, max_len - 1);

        // Instructions
        tb_printf(x + 3, y + modal_height - 2, TB_YELLOW, TB_DEFAULT,
                  "Enter: Confirm  |  Esc: Cancel");

        tb_present();

        // Handle input
        struct tb_event ev;
        tb_poll_event(&ev);

        if (ev.type == TB_EVENT_KEY) {
            if (ev.key == TB_KEY_ENTER) {
                if (cursor_pos > 0) {
                    strncpy(output_buffer, buffer, max_len - 1);
                    output_buffer[max_len - 1] = '\0';
                    memset(buffer, 0, sizeof(buffer));
                    return true;
                }
            } else if (ev.key == TB_KEY_ESC) {
                memset(buffer, 0, sizeof(buffer));
                return false;
            } else if (ev.key == TB_KEY_BACKSPACE || ev.key == TB_KEY_BACKSPACE2) {
                if (cursor_pos > 0) {
                    cursor_pos--;
                    buffer[cursor_pos] = '\0';
                }
            } else if (ev.ch && cursor_pos < (int)max_len - 1 && cursor_pos < 254) {
                buffer[cursor_pos++] = ev.ch;
                buffer[cursor_pos] = '\0';
            }
        }
    }
}

// ============================================================================
// Connection Action Handlers
// ============================================================================

/**
 * @brief Refresh current connection status
 */
static void refresh_connection_status(void) {
    current_connection_status = get_connection_status();
}

/**
 * @brief Refresh hotspot list
 */
static void refresh_hotspot_list(void) {
    hotspot_manager_init();
    hotspot_list_configs(&current_hotspots);
}

/**
 * @brief Render hotspot list panel
 */
static void render_hotspot_list(tui_panel_t *panel, const hotspot_list_t *hotspots) {
    int visible_lines = panel->height - 2;
    int start = panel->scroll_offset;
    int end = start + visible_lines;
    if (end > hotspots->count) end = hotspots->count;

    for (int i = start; i < end; i++) {
        int y = panel->y + 1 + (i - start);
        int x = panel->x + 2;

        uintptr_t fg = TB_WHITE;
        uintptr_t bg = TB_DEFAULT;

        if (i == panel->selected && panel->is_active) {
            fg = TB_BLACK;
            bg = TB_WHITE;
        }

        // Get hotspot status
        hotspot_status_t status = {0};
        hotspot_get_status(hotspots->hotspots[i].name, &status);

        // Status indicator (● = running, ○ = stopped)
        const char *indicator = " ";
        uintptr_t indicator_fg = fg;
        const char *state_text = "Stopped";

        if (status.state == HOTSPOT_STATE_ACTIVE) {
            indicator = "●";
            indicator_fg = (i == panel->selected && panel->is_active) ? fg : TB_GREEN | TB_BOLD;
            state_text = "Running";
        } else {
            indicator = "○";
        }

        // Selection arrow
        const char *arrow = (i == panel->selected && panel->is_active) ? "→" : " ";

        // Format: "● → Name | SSID | Running | 3 clients"
        char display_line[256];
        if (status.state == HOTSPOT_STATE_ACTIVE && status.client_count > 0) {
            snprintf(display_line, sizeof(display_line),
                     "%s %s %-15s  %-15s  %-8s  %d clients",
                     indicator, arrow,
                     hotspots->hotspots[i].name,
                     hotspots->hotspots[i].ssid,
                     state_text,
                     status.client_count);
        } else {
            snprintf(display_line, sizeof(display_line),
                     "%s %s %-15s  %-15s  %-8s",
                     indicator, arrow,
                     hotspots->hotspots[i].name,
                     hotspots->hotspots[i].ssid,
                     state_text);
        }

        // Print with indicator in appropriate color
        tb_printf(x, y, indicator_fg, bg, "%s", indicator);
        tb_printf(x + 2, y, fg, bg, "%s", arrow);
        tb_printf(x + 4, y, fg, bg, "%-15s  %-15s  %-8s",
                  hotspots->hotspots[i].name,
                  hotspots->hotspots[i].ssid,
                  state_text);

        if (status.state == HOTSPOT_STATE_ACTIVE && status.client_count > 0) {
            tb_printf(x + 49, y, fg, bg, " %d clients", status.client_count);
        }
    }

    // Scroll indicator
    if (hotspots->count > visible_lines && hotspots->count > 0) {
        int indicator_y = panel->y + 1;
        int progress = (panel->selected * (visible_lines - 1)) / (hotspots->count - 1);
        tb_set_cell(panel->x + panel->width - 2, indicator_y + progress, 0x2588, TB_CYAN, TB_DEFAULT);
    }
}

/**
 * @brief Handle connect action for selected network
 * @param network Network to connect to
 * @return true if should continue main loop, false to exit
 */
static bool handle_connect_action(const network_info_t *network) {
    if (!network) return true;

    char confirm_msg[256];

    // Check if already connected to this network
    if (current_connection_status.is_connected &&
        strcmp(current_connection_status.connected_ssid, network->ssid) == 0) {
        // Already connected - ask to disconnect
        snprintf(confirm_msg, sizeof(confirm_msg),
                 "Already connected to '%s'. Disconnect?", network->ssid);

        if (!draw_confirmation_modal(confirm_msg, false)) {
            return true;  // User cancelled
        }

        // Disconnect (no progress modal needed - it's quick)
        wterm_result_t result = disconnect_current_network();

        if (result == WTERM_SUCCESS) {
            refresh_connection_status();
            // User will see ✓ removed from network list
        } else {
            draw_message_modal("Failed to disconnect", true, 0);
        }

        return true;
    }

    // Check if network requires password (no confirmation modal - user already pressed 'c')
    bool is_secured = network_requires_password(network->security);
    bool is_saved = is_saved_connection(network->ssid);

    char password[256] = {0};

    // Get password if needed
    if (is_secured && !is_saved) {
        if (!draw_password_input_modal(network->ssid, password, sizeof(password))) {
            // User cancelled password input, just return to network list
            return true;
        }
    }

    // Prepare thread data for background connection
    connection_thread_data_t thread_data = {0};
    safe_string_copy(thread_data.ssid, network->ssid, sizeof(thread_data.ssid));
    if (is_secured && !is_saved) {
        safe_string_copy(thread_data.password, password, sizeof(thread_data.password));
        thread_data.is_open = false;
        thread_data.is_saved = false;
        // Clear password from stack memory
        memset(password, 0, sizeof(password));
    } else {
        thread_data.is_open = !is_secured;
        thread_data.is_saved = is_saved;
    }

    // Start connection in background thread
    pthread_t conn_thread;
    if (pthread_create(&conn_thread, NULL, connection_thread_func, &thread_data) != 0) {
        draw_message_modal("Failed to start connection thread", true, 0);
        return true;
    }

    // Poll for ESC key while connection is in progress
    int width = tb_width();
    int height = tb_height();
    bool cancelled_by_user = false;

    // Poll until thread is done
    while (!thread_data.is_done) {
        // Show cancellable progress message
        tb_clear();
        char msg[256];
        if (cancelled_by_user) {
            snprintf(msg, sizeof(msg), "Cancelling connection to '%s'...", network->ssid);
            tb_printf(width / 2 - strlen(msg) / 2, height / 2, TB_YELLOW | TB_BOLD, TB_DEFAULT, "%s", msg);
        } else {
            snprintf(msg, sizeof(msg), "Connecting to '%s'... (ESC to cancel)", network->ssid);
            tb_printf(width / 2 - strlen(msg) / 2, height / 2, TB_CYAN | TB_BOLD, TB_DEFAULT, "%s", msg);
        }
        tb_present();

        // Check for ESC key (100ms timeout to match connection polling)
        struct tb_event ev;
        int result = tb_peek_event(&ev, 100);
        if (result > 0 && ev.type == TB_EVENT_KEY && !cancelled_by_user) {
            if (ev.key == TB_KEY_ESC || ev.key == TB_KEY_CTRL_C) {
                request_connection_cancel();
                cancelled_by_user = true;
            }
        }
    }

    // Wait for thread to fully finish (should be immediate at this point)
    pthread_join(conn_thread, NULL);

    // Get connection result
    connection_result_t conn_result = thread_data.result;

    // Handle cancellation separately (no error modal needed)
    if (conn_result.result == WTERM_ERROR_CANCELLED) {
        // User cancelled - just return to network list
        // Brief message already shown during cancellation
        return true;
    }

    // Show result (brief flash, no key press required)
    if (conn_result.result == WTERM_SUCCESS) {
        refresh_connection_status();
        // Just show a brief success message (will be visible when returning to list)
        // User will see the ✓ indicator next to the network
    } else {
        // Only show error modal if failed
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Failed: %s", conn_result.error_message);
        draw_message_modal(error_msg, true, 0);
    }

    return true;
}

/**
 * @brief Handle disconnect action
 * @return true if should continue main loop, false to exit
 */
static bool handle_disconnect_action(void) {
    if (!current_connection_status.is_connected) {
        draw_message_modal("Not connected to any network", false, 0);
        return true;
    }

    char confirm_msg[256];
    snprintf(confirm_msg, sizeof(confirm_msg),
             "Disconnect from '%s'?", current_connection_status.connected_ssid);

    if (!draw_confirmation_modal(confirm_msg, false)) {
        return true;  // User cancelled
    }

    // Disconnect (no progress modal - it's quick)
    wterm_result_t result = disconnect_current_network();

    if (result == WTERM_SUCCESS) {
        refresh_connection_status();
        // User will see ✓ removed from network
    } else {
        draw_message_modal("Failed to disconnect", true, 0);
    }

    return true;
}

// ============================================================================
// Hotspot Action Handlers
// ============================================================================

/**
 * @brief Handle hotspot toggle (start/stop) action
 * @param config Hotspot configuration
 * @return true if should continue main loop
 */
static bool handle_hotspot_toggle(const hotspot_config_t *config) {
    if (!config) return true;

    // Get current status
    hotspot_status_t status = {0};
    hotspot_get_status(config->name, &status);

    char confirm_msg[256];
    if (status.state == HOTSPOT_STATE_ACTIVE) {
        // Hotspot is running - ask to stop
        snprintf(confirm_msg, sizeof(confirm_msg),
                 "Stop hotspot '%s' (%s)?", config->name, config->ssid);

        if (!draw_confirmation_modal(confirm_msg, false)) {
            return true;  // User cancelled
        }

        // Stop hotspot
        draw_progress_modal("Stopping hotspot...");
        wterm_result_t result = hotspot_stop(config->name);

        if (result == WTERM_SUCCESS) {
            draw_message_modal("Hotspot stopped successfully", false, 0);
        } else {
            draw_message_modal("Failed to stop hotspot", true, 0);
        }
    } else {
        // Hotspot is stopped - ask to start
        snprintf(confirm_msg, sizeof(confirm_msg),
                 "Start hotspot '%s' (%s)?", config->name, config->ssid);

        if (!draw_confirmation_modal(confirm_msg, true)) {
            return true;  // User cancelled
        }

        // Start hotspot
        draw_progress_modal("Starting hotspot...");
        hotspot_status_t start_status = {0};
        wterm_result_t result = hotspot_start(config->name, &start_status);

        if (result == WTERM_SUCCESS) {
            draw_message_modal("Hotspot started successfully", false, 0);
        } else {
            draw_message_modal("Failed to start hotspot", true, 0);
        }
    }

    return true;
}

/**
 * @brief Handle hotspot creation wizard
 * @return true if should continue main loop
 */
static bool handle_hotspot_create(void) {
    hotspot_config_t new_config = {0};

    // Step 1: Get SSID (will also be used as the configuration name)
    if (!draw_text_input_modal("Create Hotspot (1/4)",
                                "Enter the WiFi network name (SSID):",
                                "e.g., MyWiFi",
                                new_config.ssid,
                                sizeof(new_config.ssid))) {
        return true;  // User cancelled
    }

    // Use SSID as configuration name
    strncpy(new_config.name, new_config.ssid, sizeof(new_config.name) - 1);
    new_config.name[sizeof(new_config.name) - 1] = '\0';

    // Step 2: Select security type
    const char *security_options[] = {
        "Open (No password)",
        "Secured (WPA2-PSK password)"
    };
    int security_choice = draw_selection_modal("Create Hotspot (2/4)",
                                                "Select security type:",
                                                security_options,
                                                2,
                                                1);  // Default to secured

    if (security_choice < 0) {
        return true;  // User cancelled
    }

    new_config.security_type = (security_choice == 0) ? WIFI_SECURITY_NONE : WIFI_SECURITY_WPA2;

    // Step 3: Get password if secured
    if (new_config.security_type == WIFI_SECURITY_WPA2) {
        if (!draw_password_input_modal(new_config.ssid,
                                        new_config.password,
                                        sizeof(new_config.password))) {
            return true;  // User cancelled
        }
    }

    // Step 4: Select band
    const char *band_options[] = {
        "2.4GHz (802.11g) - Better range",
        "5GHz (802.11a) - Faster speed"
    };
    int band_choice = draw_selection_modal("Create Hotspot (3/4)",
                                            "Select WiFi band:",
                                            band_options,
                                            2,
                                            0);  // Default to 2.4GHz

    if (band_choice < 0) {
        return true;  // User cancelled
    }

    new_config.is_5ghz = (band_choice == 1);  // true for 5GHz, false for 2.4GHz

    // Step 5: Get WiFi interfaces
    interface_info_t interfaces[8] = {0};
    int interface_count = 0;
    wterm_result_t result = hotspot_get_interface_list(interfaces, 8, &interface_count);

    if (result != WTERM_SUCCESS || interface_count == 0) {
        draw_message_modal("No WiFi interfaces available", true, 0);
        return true;
    }

    // Build interface selection list
    const char *interface_options[8];
    for (int i = 0; i < interface_count; i++) {
        interface_options[i] = interfaces[i].name;
    }

    int iface_choice = draw_selection_modal("Create Hotspot (4/4)",
                                             "Select WiFi interface:",
                                             interface_options,
                                             interface_count,
                                             0);

    if (iface_choice < 0) {
        return true;  // User cancelled
    }

    strncpy(new_config.wifi_interface, interfaces[iface_choice].name, sizeof(new_config.wifi_interface) - 1);

    // Create the hotspot configuration
    draw_progress_modal("Creating hotspot configuration...");
    result = hotspot_create_config(&new_config);

    if (result == WTERM_SUCCESS) {
        draw_message_modal("Hotspot created successfully", false, 0);
    } else {
        draw_message_modal("Failed to create hotspot", true, 0);
    }

    return true;
}

/**
 * @brief Handle hotspot deletion with confirmation
 * @param config Hotspot configuration to delete
 * @return true if should continue main loop
 */
static bool handle_hotspot_delete(const hotspot_config_t *config) {
    if (!config) return true;

    // Check if hotspot is currently running
    hotspot_status_t status = {0};
    hotspot_get_status(config->name, &status);

    if (status.state == HOTSPOT_STATE_ACTIVE) {
        draw_message_modal("Cannot delete running hotspot. Stop it first.", true, 0);
        return true;
    }

    // Confirmation
    char confirm_msg[256];
    snprintf(confirm_msg, sizeof(confirm_msg),
             "Delete hotspot '%s' (%s)?", config->name, config->ssid);

    if (!draw_confirmation_modal(confirm_msg, false)) {
        return true;  // User cancelled
    }

    // Delete hotspot
    wterm_result_t result = hotspot_delete_config(config->name);

    if (result == WTERM_SUCCESS) {
        draw_message_modal("Hotspot deleted successfully", false, 0);
    } else {
        draw_message_modal("Failed to delete hotspot", true, 0);
    }

    return true;
}

// ============================================================================
// Public API Implementation
// ============================================================================

bool tui_is_available(void) {
    // Check if we're running in a terminal
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
}

wterm_result_t tui_init(void) {
    if (tui_initialized) {
        return WTERM_SUCCESS;
    }

    int ret = tb_init();
    if (ret != TB_OK) {
        return WTERM_ERROR_GENERAL;
    }

    tui_initialized = true;

    // Refresh connection status on init
    refresh_connection_status();

    // Refresh hotspot list on init
    refresh_hotspot_list();

    return WTERM_SUCCESS;
}

void tui_shutdown(void) {
    if (tui_initialized) {
        tb_shutdown();
        tui_initialized = false;
    }
}

bool tui_select_network(const network_list_t *networks,
                        char *selected_ssid,
                        size_t buffer_size) {
    if (!networks || !selected_ssid || buffer_size == 0) {
        return false;
    }

    if (!tui_initialized) {
        if (tui_init() != WTERM_SUCCESS) {
            return false;
        }
    }

    // Filter and deduplicate networks
    network_list_t filtered_networks = {0};

    for (int i = 0; i < networks->count; i++) {
        // Skip empty SSIDs
        if (networks->networks[i].ssid[0] == '\0' ||
            strlen(networks->networks[i].ssid) == 0) {
            continue;
        }

        // Check if this SSID already exists in filtered list
        bool duplicate = false;
        int existing_idx = -1;
        for (int j = 0; j < filtered_networks.count; j++) {
            if (strcmp(filtered_networks.networks[j].ssid, networks->networks[i].ssid) == 0) {
                duplicate = true;
                existing_idx = j;
                break;
            }
        }

        if (duplicate) {
            // Keep the one with stronger signal
            int current_signal = atoi(networks->networks[i].signal);
            int existing_signal = atoi(filtered_networks.networks[existing_idx].signal);
            if (current_signal > existing_signal) {
                // Replace with stronger signal
                filtered_networks.networks[existing_idx] = networks->networks[i];
            }
        } else {
            // Add new network
            if (filtered_networks.count < MAX_NETWORKS) {
                filtered_networks.networks[filtered_networks.count++] = networks->networks[i];
            }
        }
    }

    int width = tb_width();
    int height = tb_height();

    // Calculate panel heights (3 panels: Networks, Hotspots, Keybindings)
    int keybindings_height = 3;  // Fixed small height for keybindings
    int panel1_height = (height - keybindings_height) / 2;  // 50% for networks
    int panel2_height = height - panel1_height - keybindings_height;  // 50% for hotspots

    if (panel1_height < 8) panel1_height = 8;
    if (panel2_height < 8) panel2_height = 8;

    tui_panel_t panels[3] = {
        {0, 0, width, panel1_height, "Available Networks", true, 0, 0, filtered_networks.count},
        {0, panel1_height, width, panel2_height, "Hotspots", false, 0, 0, current_hotspots.count},
        {0, panel1_height + panel2_height, width, keybindings_height, "Keybindings", false, 0, 0, 0}
    };

    bool running = true;
    int active_panel = 0;  // 0=networks, 1=hotspots
    bool show_help = false;
    bool network_selected = false;

    while (running) {
        tb_clear();

        panels[0].is_active = (active_panel == 0);
        panels[1].is_active = (active_panel == 1);
        panels[2].is_active = false;

        // Draw panels
        for (int i = 0; i < 3; i++) {
            draw_panel_border(&panels[i]);
            clear_panel_content(&panels[i]);
        }

        // Render available networks (using filtered list)
        render_available_networks(&panels[0], &filtered_networks);

        // Render hotspot list
        render_hotspot_list(&panels[1], &current_hotspots);

        // Keybindings panel (dynamic based on active panel)
        if (active_panel == 0) {
            tb_printf(panels[2].x + 2, panels[2].y + 1, TB_YELLOW, TB_DEFAULT,
                      "j/↓:Down k/↑:Up Tab:Switch c/Enter:Connect d:Disconnect r:Rescan ?:Help q:Quit");
        } else {
            tb_printf(panels[2].x + 2, panels[2].y + 1, TB_YELLOW, TB_DEFAULT,
                      "j/↓:Down k/↑:Up Tab:Switch Enter:Start/Stop n:Create d:Delete r:Refresh ?:Help q:Quit");
        }

        // Status line (dynamic based on active panel)
        if (active_panel == 0) {
            const char *selected_network = "";
            if (filtered_networks.count > 0 && panels[0].selected < filtered_networks.count) {
                selected_network = filtered_networks.networks[panels[0].selected].ssid;
            }
            tb_printf(0, height - 1, TB_GREEN, TB_DEFAULT,
                      " wterm TUI | Panel 1/2 | Network: %s [%d/%d]",
                      selected_network,
                      panels[0].selected + 1,
                      panels[0].item_count);
        } else {
            const char *selected_hotspot = "";
            if (current_hotspots.count > 0 && panels[1].selected < current_hotspots.count) {
                selected_hotspot = current_hotspots.hotspots[panels[1].selected].name;
            }
            tb_printf(0, height - 1, TB_GREEN, TB_DEFAULT,
                      " wterm TUI | Panel 2/2 | Hotspot: %s [%d/%d]",
                      selected_hotspot,
                      panels[1].selected + 1,
                      panels[1].item_count);
        }

        if (show_help) {
            draw_help_modal();
        }

        tb_present();

        struct tb_event ev;
        tb_poll_event(&ev);

        if (show_help) {
            show_help = false;
            continue;
        }

        if (ev.type == TB_EVENT_KEY) {
            switch (ev.key) {
                case TB_KEY_TAB:
                    active_panel = (active_panel + 1) % 2;
                    break;
                case TB_KEY_ARROW_UP:
                    move_selection(&panels[active_panel], -1);
                    break;
                case TB_KEY_ARROW_DOWN:
                    move_selection(&panels[active_panel], 1);
                    break;
                case TB_KEY_ENTER:
                    // Panel-specific Enter action
                    if (active_panel == 0) {
                        // Network panel: Connect to network
                        if (filtered_networks.count > 0 && panels[0].selected < filtered_networks.count) {
                            network_info_t *selected_net = (network_info_t*)&filtered_networks.networks[panels[0].selected];
                            handle_connect_action(selected_net);
                        }
                    } else if (active_panel == 1) {
                        // Hotspot panel: Toggle hotspot (start/stop)
                        if (current_hotspots.count > 0 && panels[1].selected < current_hotspots.count) {
                            handle_hotspot_toggle(&current_hotspots.hotspots[panels[1].selected]);
                            refresh_hotspot_list();
                            // Update panel item count
                            panels[1].item_count = current_hotspots.count;
                        }
                    }
                    break;
                default:
                    if (ev.ch == 'q') {
                        running = false;
                    } else if (ev.ch == '?') {
                        show_help = !show_help;
                    } else if (ev.ch == 'h') {
                        // Switch to previous panel
                        active_panel = (active_panel == 0) ? 1 : 0;
                    } else if (ev.ch == 'l') {
                        // Switch to next panel
                        active_panel = (active_panel + 1) % 2;
                    } else if (ev.ch == 'c' && active_panel == 0) {
                        // Connect to selected network (network panel only)
                        if (filtered_networks.count > 0 && panels[0].selected < filtered_networks.count) {
                            network_info_t *selected_net = (network_info_t*)&filtered_networks.networks[panels[0].selected];
                            handle_connect_action(selected_net);
                        }
                    } else if (ev.ch == 'd') {
                        // Panel-specific delete/disconnect
                        if (active_panel == 0) {
                            // Network panel: Disconnect
                            handle_disconnect_action();
                        } else if (active_panel == 1) {
                            // Hotspot panel: Delete hotspot
                            if (current_hotspots.count > 0 && panels[1].selected < current_hotspots.count) {
                                handle_hotspot_delete(&current_hotspots.hotspots[panels[1].selected]);
                                refresh_hotspot_list();
                                panels[1].item_count = current_hotspots.count;
                                // Adjust selection if needed
                                if (panels[1].selected >= panels[1].item_count && panels[1].item_count > 0) {
                                    panels[1].selected = panels[1].item_count - 1;
                                }
                            }
                        }
                    } else if (ev.ch == 'n' && active_panel == 1) {
                        // Create new hotspot (hotspot panel only)
                        handle_hotspot_create();
                        refresh_hotspot_list();
                        panels[1].item_count = current_hotspots.count;
                    } else if (ev.ch == 'r') {
                        // Panel-specific refresh
                        if (active_panel == 0) {
                            // Network panel: Rescan
                            snprintf(selected_ssid, buffer_size, "RESCAN");
                            network_selected = true;
                            running = false;
                        } else {
                            // Hotspot panel: Refresh list
                            refresh_hotspot_list();
                            panels[1].item_count = current_hotspots.count;
                        }
                    } else if (ev.ch == 'j') {
                        move_selection(&panels[active_panel], 1);
                    } else if (ev.ch == 'k') {
                        move_selection(&panels[active_panel], -1);
                    } else if (ev.ch == 'g') {
                        panels[active_panel].selected = 0;
                        panels[active_panel].scroll_offset = 0;
                    } else if (ev.ch == 'G') {
                        panels[active_panel].selected = panels[active_panel].item_count - 1;
                        int visible = panels[active_panel].height - 2;
                        panels[active_panel].scroll_offset = panels[active_panel].item_count - visible;
                        if (panels[active_panel].scroll_offset < 0) {
                            panels[active_panel].scroll_offset = 0;
                        }
                    }
                    break;
            }
        }
    }

    return network_selected;
}

bool tui_get_password(const char *ssid, char *password_out, size_t buffer_size) {
    // TODO: Implement password input modal
    // For now, fall back to simple getpass
    printf("Password for %s: ", ssid);
    fflush(stdout);

    char *pass = getpass("");
    if (pass && strlen(pass) > 0) {
        snprintf(password_out, buffer_size, "%s", pass);
        memset(pass, 0, strlen(pass));
        return true;
    }

    return false;
}

void tui_show_message(const char *message, bool wait_for_key) {
    // TODO: Implement message modal
    printf("%s\n", message);
    if (wait_for_key) {
        getchar();
    }
}

void tui_show_loading(const char *message) {
    // TODO: Implement loading animation
    printf("%s\n", message);
}

void tui_hide_loading(void) {
    // TODO: Implement
}
