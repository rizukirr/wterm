/**
 * @file tui_test.c
 * @brief Standalone test program for termbox2 TUI
 *
 * Phase 3: List rendering with scrolling and navigation
 * Compile: see CMakeLists.txt
 * Run: ./bin/tui_test (requires real terminal)
 */

#define TB_IMPL
#include "../../include/external/termbox2.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// Network data structure
typedef struct {
    const char *ssid;
    int signal;         // 0-100
    const char *security;
    bool is_saved;
} network_t;

// Panel structure
typedef struct {
    int x, y;           // Top-left position
    int width, height;  // Dimensions
    const char *title;  // Panel title
    bool is_active;     // Active state
    int selected;       // Selected item index
    int scroll_offset;  // Scroll offset
    int item_count;     // Number of items
} panel_t;

// Draw horizontal line
static void draw_hline(int x, int y, int width, uint32_t ch, uintptr_t fg, uintptr_t bg) {
    for (int i = 0; i < width; i++) {
        tb_set_cell(x + i, y, ch, fg, bg);
    }
}

// Draw vertical line
static void draw_vline(int x, int y, int height, uint32_t ch, uintptr_t fg, uintptr_t bg) {
    for (int i = 0; i < height; i++) {
        tb_set_cell(x, y + i, ch, fg, bg);
    }
}

// Draw panel border with title
static void draw_panel_border(panel_t *panel) {
    uintptr_t fg = panel->is_active ? TB_CYAN | TB_BOLD : TB_WHITE;
    uintptr_t bg = TB_DEFAULT;

    // Top border
    tb_set_cell(panel->x, panel->y, 0x250C, fg, bg);  // ┌
    draw_hline(panel->x + 1, panel->y, panel->width - 2, 0x2500, fg, bg);  // ─
    tb_set_cell(panel->x + panel->width - 1, panel->y, 0x2510, fg, bg);  // ┐

    // Title in top border
    int title_len = strlen(panel->title);
    int title_x = panel->x + 2;
    tb_printf(title_x, panel->y, fg, bg, " %s ", panel->title);

    // Draw remaining top border after title
    int title_end = title_x + title_len + 2;
    if (title_end < panel->x + panel->width - 1) {
        draw_hline(title_end, panel->y, panel->x + panel->width - 1 - title_end, 0x2500, fg, bg);
    }

    // Side borders
    draw_vline(panel->x, panel->y + 1, panel->height - 2, 0x2502, fg, bg);  // │
    draw_vline(panel->x + panel->width - 1, panel->y + 1, panel->height - 2, 0x2502, fg, bg);  // │

    // Bottom border
    tb_set_cell(panel->x, panel->y + panel->height - 1, 0x2514, fg, bg);  // └
    draw_hline(panel->x + 1, panel->y + panel->height - 1, panel->width - 2, 0x2500, fg, bg);  // ─
    tb_set_cell(panel->x + panel->width - 1, panel->y + panel->height - 1, 0x2518, fg, bg);  // ┘
}

// Clear panel content area
static void clear_panel_content(panel_t *panel) {
    for (int y = 1; y < panel->height - 1; y++) {
        for (int x = 1; x < panel->width - 1; x++) {
            tb_set_cell(panel->x + x, panel->y + y, ' ', TB_DEFAULT, TB_DEFAULT);
        }
    }
}

// Generate signal bars based on percentage
static void get_signal_bars(int signal, char *buf, size_t size) {
    if (size < 6) return;

    int bars = (signal / 25);  // 0-4 bars
    if (bars > 4) bars = 4;

    buf[0] = '[';
    buf[1] = (bars >= 1) ? 0xE2 : ' ';  // █
    buf[2] = (bars >= 1) ? 0x96 : ' ';
    buf[3] = (bars >= 1) ? 0x88 : ' ';
    buf[4] = (bars >= 2) ? 0xE2 : ' ';
    buf[5] = (bars >= 2) ? 0x96 : ' ';
    buf[6] = (bars >= 2) ? 0x88 : ' ';
    buf[7] = (bars >= 3) ? 0xE2 : ' ';
    buf[8] = (bars >= 3) ? 0x96 : ' ';
    buf[9] = (bars >= 3) ? 0x88 : ' ';
    buf[10] = (bars >= 4) ? 0xE2 : ' ';
    buf[11] = (bars >= 4) ? 0x96 : ' ';
    buf[12] = (bars >= 4) ? 0x88 : ' ';
    buf[13] = ']';
    buf[14] = '\0';
}

// Render signal bar (simplified ASCII version)
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

// Render saved networks list
static void render_saved_networks(panel_t *panel, const char **saved_nets, int count) {
    int visible_lines = panel->height - 2;  // Exclude borders
    int start = panel->scroll_offset;
    int end = start + visible_lines;
    if (end > count) end = count;

    for (int i = start; i < end; i++) {
        int y = panel->y + 1 + (i - start);
        int x = panel->x + 2;

        uintptr_t fg = TB_WHITE;
        uintptr_t bg = TB_DEFAULT;

        // Highlight selected item
        if (i == panel->selected && panel->is_active) {
            fg = TB_BLACK;
            bg = TB_WHITE;
        }

        // Draw bullet point and SSID
        tb_printf(x, y, fg, bg, "• %-30s", saved_nets[i]);
    }

    // Show scroll indicator if needed
    if (count > visible_lines) {
        int indicator_y = panel->y + 1;
        int progress = (panel->selected * (visible_lines - 1)) / (count - 1);
        tb_set_cell(panel->x + panel->width - 2, indicator_y + progress, 0x2588, TB_CYAN, TB_DEFAULT);
    }
}

// Render available networks list
static void render_available_networks(panel_t *panel, network_t *networks, int count) {
    int visible_lines = panel->height - 2;  // Exclude borders
    int start = panel->scroll_offset;
    int end = start + visible_lines;
    if (end > count) end = count;

    for (int i = start; i < end; i++) {
        int y = panel->y + 1 + (i - start);
        int x = panel->x + 2;

        uintptr_t fg = TB_WHITE;
        uintptr_t bg = TB_DEFAULT;

        // Highlight selected item
        if (i == panel->selected && panel->is_active) {
            fg = TB_BLACK;
            bg = TB_WHITE;
        }

        // Draw selection arrow
        const char *arrow = (i == panel->selected && panel->is_active) ? "→" : " ";

        // Draw SSID (left-aligned, 20 chars)
        char ssid_buf[32];
        snprintf(ssid_buf, sizeof(ssid_buf), "%-20s", networks[i].ssid);
        tb_printf(x, y, fg, bg, "%s %s", arrow, ssid_buf);

        // Draw signal bar
        render_signal_bar(x + 23, y, networks[i].signal, fg, bg);

        // Draw signal percentage and security
        tb_printf(x + 30, y, fg, bg, " %3d%%  %-6s",
                  networks[i].signal, networks[i].security);

        // Show saved indicator if applicable
        if (networks[i].is_saved) {
            tb_set_cell(x + 45, y, 0x2022, TB_GREEN, bg);  // •
        }
    }

    // Show scroll indicator if needed
    if (count > visible_lines) {
        int indicator_y = panel->y + 1;
        if (count > 1) {
            int progress = (panel->selected * (visible_lines - 1)) / (count - 1);
            tb_set_cell(panel->x + panel->width - 2, indicator_y + progress, 0x2588, TB_CYAN, TB_DEFAULT);
        }
    }
}

// Handle panel navigation
static void move_selection(panel_t *panel, int direction) {
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

// Draw help modal overlay
static void draw_help_modal(void) {
    int width = tb_width();
    int height = tb_height();

    // Modal dimensions (centered)
    int modal_width = 60;
    int modal_height = 18;
    int modal_x = (width - modal_width) / 2;
    int modal_y = (height - modal_height) / 2;

    // Draw modal background (same as panels - default/black)
    uintptr_t bg = TB_DEFAULT;
    uintptr_t fg = TB_WHITE;
    for (int y = 0; y < modal_height; y++) {
        for (int x = 0; x < modal_width; x++) {
            tb_set_cell(modal_x + x, modal_y + y, ' ', fg, bg);
        }
    }

    // Draw border (same style as panels - cyan)
    uintptr_t border_fg = TB_CYAN | TB_BOLD;

    // Top border
    tb_set_cell(modal_x, modal_y, 0x250C, border_fg, bg);  // ┌
    draw_hline(modal_x + 1, modal_y, modal_width - 2, 0x2500, border_fg, bg);  // ─
    tb_set_cell(modal_x + modal_width - 1, modal_y, 0x2510, border_fg, bg);  // ┐

    // Title
    const char *title = " HELP ";
    int title_x = modal_x + (modal_width - strlen(title)) / 2;
    tb_printf(title_x, modal_y, TB_YELLOW | TB_BOLD, bg, "%s", title);

    // Side borders
    draw_vline(modal_x, modal_y + 1, modal_height - 2, 0x2502, border_fg, bg);  // │
    draw_vline(modal_x + modal_width - 1, modal_y + 1, modal_height - 2, 0x2502, border_fg, bg);  // │

    // Bottom border
    tb_set_cell(modal_x, modal_y + modal_height - 1, 0x2514, border_fg, bg);  // └
    draw_hline(modal_x + 1, modal_y + modal_height - 1, modal_width - 2, 0x2500, border_fg, bg);  // ─
    tb_set_cell(modal_x + modal_width - 1, modal_y + modal_height - 1, 0x2518, border_fg, bg);  // ┘

    // Content
    int content_x = modal_x + 3;
    int content_y = modal_y + 2;

    tb_printf(content_x, content_y++, TB_CYAN | TB_BOLD, bg, "Navigation:");
    tb_printf(content_x, content_y++, fg, bg, "  j / ↓         Move down in current panel");
    tb_printf(content_x, content_y++, fg, bg, "  k / ↑         Move up in current panel");
    tb_printf(content_x, content_y++, fg, bg, "  g             Jump to top of list");
    tb_printf(content_x, content_y++, fg, bg, "  G             Jump to bottom of list");
    content_y++;

    tb_printf(content_x, content_y++, TB_CYAN | TB_BOLD, bg, "Panel Switching:");
    tb_printf(content_x, content_y++, fg, bg, "  Tab           Switch to next panel");
    tb_printf(content_x, content_y++, fg, bg, "  h             Switch to previous panel");
    tb_printf(content_x, content_y++, fg, bg, "  l             Switch to next panel");
    content_y++;

    tb_printf(content_x, content_y++, TB_CYAN | TB_BOLD, bg, "Actions:");
    tb_printf(content_x, content_y++, fg, bg, "  c / Enter     Connect to selected network");
    tb_printf(content_x, content_y++, fg, bg, "  d             Disconnect from network");
    tb_printf(content_x, content_y++, fg, bg, "  r             Rescan networks");
    tb_printf(content_x, content_y++, fg, bg, "  ?             Toggle this help");
    tb_printf(content_x, content_y++, fg, bg, "  q / Esc       Quit application");

    // Footer
    tb_printf(modal_x + (modal_width - 30) / 2, modal_y + modal_height - 2,
              TB_YELLOW | TB_BOLD, bg, "Press any key to close...");
}

// Main function
int main(void) {
    int ret = tb_init();
    if (ret != TB_OK) {
        fprintf(stderr, "tb_init() failed with error code %d\n", ret);
        fprintf(stderr, "Make sure you're running in a real terminal (not via pipe)\n");
        return 1;
    }

    // Dummy data: Saved networks
    const char *saved_nets[] = {
        "MyHomeWiFi",
        "Office5G",
        "CoffeeShop",
        "Library-Guest",
        "FriendHouse"
    };
    int saved_count = 5;

    // Dummy data: Available networks (more items to test scrolling)
    network_t available_nets[] = {
        {"MyHomeWiFi", 85, "WPA2", true},
        {"Office5G", 72, "WPA2", true},
        {"OpenWiFi", 45, "Open", false},
        {"Neighbor", 20, "WPA3", false},
        {"POCO F4", 68, "Open", false},
        {"CoffeeShop", 55, "WPA2", true},
        {"Library-Guest", 62, "Open", true},
        {"EdgeRouter", 30, "WPA2", false},
        {"Guest-5G", 48, "WPA2", false},
        {"TestNet", 15, "WPA3", false},
        {"FriendHouse", 90, "WPA2", true},
        {"MobileHotspot", 78, "WPA2", false},
        {"PublicWiFi", 25, "Open", false},
        {"SecureNet", 40, "WPA3", false},
        {"Workshop", 35, "WPA2", false}
    };
    int available_count = 15;

    // Get terminal dimensions
    int width = tb_width();
    int height = tb_height();

    // Calculate panel heights
    int panel3_height = 3;  // Fixed small height for keybindings (just 3 lines)
    int panel1_height = height / 5;  // 20% for saved networks
    int panel2_height = height - panel1_height - panel3_height;  // Rest for available

    // Ensure minimum heights
    if (panel1_height < 5) panel1_height = 5;
    if (panel2_height < 8) panel2_height = 8;

    // Initialize panels with item counts
    panel_t panels[3] = {
        {0, 0, width, panel1_height, "Saved Networks (1/3)", true, 0, 0, saved_count},
        {0, panel1_height, width, panel2_height, "Available Networks (2/3)", false, 0, 0, available_count},
        {0, panel1_height + panel2_height, width, panel3_height, "Keybindings", false, 0, 0, 0}
    };

    bool running = true;
    int active_panel = 0;  // 0 or 1 (panel 2 is info-only)
    bool show_help = false;

    while (running) {
        // Clear screen
        tb_clear();

        // Update active state
        panels[0].is_active = (active_panel == 0);
        panels[1].is_active = (active_panel == 1);
        panels[2].is_active = false;  // Never active

        // Draw all panels
        for (int i = 0; i < 3; i++) {
            draw_panel_border(&panels[i]);
            clear_panel_content(&panels[i]);
        }

        // Render Panel 1 (Saved Networks)
        render_saved_networks(&panels[0], saved_nets, saved_count);

        // Render Panel 2 (Available Networks)
        render_available_networks(&panels[1], available_nets, available_count);

        // Render Panel 3 (Keybindings)
        tb_printf(panels[2].x + 2, panels[2].y + 1, TB_YELLOW, TB_DEFAULT,
                  "j/↓:Down  k/↑:Up  Tab/h/l:Switch  c/Enter:Connect  d:Disconnect  r:Rescan  ?:Help  q:Quit");

        // Status line at very bottom
        const char *selected_ssid = "";
        if (active_panel == 0 && saved_count > 0) {
            selected_ssid = saved_nets[panels[0].selected];
        } else if (active_panel == 1 && available_count > 0) {
            selected_ssid = available_nets[panels[1].selected].ssid;
        }
        tb_printf(0, height - 1, TB_GREEN, TB_DEFAULT,
                  " Phase 3: Navigation Test | Panel %d/2 | Selected: %s [%d/%d] | Terminal: %dx%d",
                  active_panel + 1, selected_ssid,
                  panels[active_panel].selected + 1, panels[active_panel].item_count,
                  width, height);

        // Draw help modal if active (drawn last, on top of everything)
        if (show_help) {
            draw_help_modal();
        }

        // Present to screen
        tb_present();

        // Wait for event
        struct tb_event ev;
        tb_poll_event(&ev);

        // If help is showing, any key closes it
        if (show_help) {
            show_help = false;
            continue;  // Skip normal key processing
        }

        // Handle keyboard input
        if (ev.type == TB_EVENT_KEY) {
            switch (ev.key) {
                case TB_KEY_ESC:
                    running = false;
                    break;
                case TB_KEY_TAB:
                    // Cycle between panel 0 and 1
                    active_panel = (active_panel + 1) % 2;
                    break;
                case TB_KEY_ARROW_UP:
                    // Move up in current panel
                    move_selection(&panels[active_panel], -1);
                    break;
                case TB_KEY_ARROW_DOWN:
                    // Move down in current panel
                    move_selection(&panels[active_panel], 1);
                    break;
                default:
                    // Handle character keys
                    if (ev.ch == 'q') {
                        running = false;
                    } else if (ev.ch == '?') {
                        // ? = toggle help
                        show_help = !show_help;
                    } else if (ev.ch == 'j') {
                        // j = down (vim-style)
                        move_selection(&panels[active_panel], 1);
                    } else if (ev.ch == 'k') {
                        // k = up (vim-style)
                        move_selection(&panels[active_panel], -1);
                    } else if (ev.ch == 'l') {
                        // l = next panel (vim-style)
                        active_panel = (active_panel + 1) % 2;
                    } else if (ev.ch == 'h') {
                        // h = previous panel (vim-style)
                        active_panel = (active_panel == 0) ? 1 : 0;
                    } else if (ev.ch == 'g') {
                        // g = go to top
                        panels[active_panel].selected = 0;
                        panels[active_panel].scroll_offset = 0;
                    } else if (ev.ch == 'G') {
                        // G = go to bottom
                        panels[active_panel].selected = panels[active_panel].item_count - 1;
                        int visible_lines = panels[active_panel].height - 2;
                        panels[active_panel].scroll_offset =
                            panels[active_panel].item_count - visible_lines;
                        if (panels[active_panel].scroll_offset < 0) {
                            panels[active_panel].scroll_offset = 0;
                        }
                    }
                    break;
            }
        }
    }

    // Cleanup
    tb_shutdown();

    printf("✓ Phase 3 test completed!\n");
    printf("Panel layout: %d / %d / %d lines\n", panel1_height, panel2_height, panel3_height);
    printf("Navigation worked with %d saved and %d available networks\n", saved_count, available_count);
    printf("Final selection: Panel %d, Item %d\n", active_panel + 1, panels[active_panel].selected + 1);

    return 0;
}
