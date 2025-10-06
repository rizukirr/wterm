#pragma once

/**
 * @file fzf_ui.h
 * @brief fzf-based user interface for WiFi network selection
 */

#include "../include/wterm/common.h"
#include "core/network_scanner.h"
#include "core/connection.h"
#include "core/hotspot_manager.h"
#include <stdbool.h>

// Removed redundant fzf_select_network function - use fzf_select_network_proper directly

/**
 * @brief Get password input for secured network
 * @param ssid Network SSID
 * @param password_out Output buffer for password
 * @param buffer_size Size of password buffer
 * @return bool true if password entered, false if cancelled
 */
bool fzf_get_password(const char* ssid, char* password_out, size_t buffer_size);

/**
 * @brief Show connection status message
 * @param message Status message to display
 */
void fzf_show_message(const char* message);

/**
 * @brief Check if fzf is available on the system
 * @return bool true if fzf command is available
 */
bool fzf_is_available(void);

/**
 * @brief Show network selection using fzf with rescan option
 * @param networks List of available networks
 * @param selected_ssid Output buffer for selected SSID
 * @param buffer_size Size of output buffer
 * @return bool true if user selected a network, false if cancelled
 */
bool fzf_select_network_proper(const network_list_t* networks, char* selected_ssid, size_t buffer_size);

/**
 * @brief Show loading animation during network scan
 * @param message Loading message to display
 */
void show_loading_animation(const char* message);

/**
 * @brief Hide loading animation and clear line
 */
void hide_loading_animation(void);

// Hotspot creation functions

/**
 * @brief Interactive hotspot creation wizard using fzf
 * @param config Output configuration structure
 * @return bool true if hotspot configuration created, false if cancelled
 */
bool fzf_create_hotspot_interactive(hotspot_config_t* config);

/**
 * @brief Select WiFi interface for hotspot using fzf
 * @param interface_out Output buffer for selected interface
 * @param buffer_size Size of interface buffer
 * @return bool true if interface selected, false if cancelled
 */
bool fzf_select_wifi_interface(char* interface_out, size_t buffer_size);

/**
 * @brief Select internet source interface using fzf
 * @param interface_out Output buffer for selected interface
 * @param buffer_size Size of interface buffer
 * @return bool true if interface selected, false if cancelled
 */
bool fzf_select_internet_source(char* interface_out, size_t buffer_size);

/**
 * @brief Get hotspot configuration interactively
 * @param config Configuration structure to fill
 * @return bool true if configuration completed, false if cancelled
 */
bool fzf_get_hotspot_config(hotspot_config_t* config);

/**
 * @brief Get text input using fzf prompt
 * @param prompt Prompt message
 * @param default_value Default value to show
 * @param output_buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return bool true if input provided, false if cancelled
 */
bool fzf_get_text_input(const char* prompt, const char* default_value,
                        char* output_buffer, size_t buffer_size);

/**
 * @brief Get secure password input
 * @param prompt Prompt message
 * @param output_buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return bool true if password provided, false if cancelled
 */
bool fzf_get_secure_input(const char* prompt, char* output_buffer, size_t buffer_size);

/**
 * @brief Show configuration confirmation dialog
 * @param config Configuration to confirm
 * @return bool true if user confirms, false if wants to modify
 */
bool fzf_confirm_hotspot_config(const hotspot_config_t* config);

