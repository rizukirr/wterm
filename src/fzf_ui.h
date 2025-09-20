#pragma once

/**
 * @file fzf_ui.h
 * @brief fzf-based user interface for WiFi network selection
 */

#include "../include/wterm/common.h"
#include "core/network_scanner.h"
#include "core/connection.h"
#include <stdbool.h>

/**
 * @brief Show network selection using fzf
 * @param networks List of available networks
 * @param selected_ssid Output buffer for selected SSID
 * @param buffer_size Size of output buffer
 * @return bool true if user selected a network, false if cancelled
 */
bool fzf_select_network(const network_list_t* networks, char* selected_ssid, size_t buffer_size);

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
 * @brief Alternative implementation that properly pipes data to fzf
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