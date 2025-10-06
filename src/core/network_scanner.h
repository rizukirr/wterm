#pragma once

/**
 * @file network_scanner.h
 * @brief WiFi network scanning and management functionality
 */

#include "../../include/wterm/common.h"

/**
 * @brief Scan for available WiFi networks
 * @param network_list Pointer to network_list_t structure to populate
 * @return wterm_result_t Result code
 */
wterm_result_t scan_wifi_networks(network_list_t *network_list);

/**
 * @brief Parse a single line of nmcli output
 * @param buffer Input line from nmcli command
 * @param network Pointer to network_info_t structure to populate
 * @return wterm_result_t Result code
 */
wterm_result_t parse_network_line(const char *buffer, network_info_t *network);

/**
 * @brief Display network list in formatted output
 * @param network_list Pointer to network_list_t structure to display
 */
void display_networks(const network_list_t *network_list);

/**
 * @brief Trigger a WiFi network rescan
 * @return wterm_result_t Result code
 */
wterm_result_t rescan_wifi_networks(void);

/**
 * @brief Trigger a WiFi network rescan with optional silent mode
 * @param silent If true, suppress status messages
 * @return wterm_result_t Result code
 */
wterm_result_t rescan_wifi_networks_silent(bool silent);