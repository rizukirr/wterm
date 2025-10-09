/**
 * @file iw_helper.h
 * @brief Helper functions for iw command operations
 *
 * This module provides wrapper functions for common iw command operations,
 * including interface detection, capability checking, and band validation.
 */

#ifndef IW_HELPER_H
#define IW_HELPER_H

#include "../../include/wterm/common.h"
#include <stdbool.h>

/**
 * @brief Get the first available WiFi interface
 * @param interface Buffer to store interface name
 * @param size Size of the buffer
 * @return true if interface found, false otherwise
 */
bool iw_get_first_wifi_interface(char *interface, size_t size);

/**
 * @brief Get all WiFi interfaces on the system
 * @param interfaces Array to store interface names
 * @param max_count Maximum number of interfaces to return
 * @param count Output: actual number of interfaces found
 * @return WTERM_SUCCESS on success, error code otherwise
 */
wterm_result_t iw_get_all_wifi_interfaces(char interfaces[][MAX_STR_INTERFACE],
                                          int max_count, int *count);

/**
 * @brief Check if an interface supports AP (Access Point) mode
 * @param interface Interface name to check
 * @param supports_ap Output: true if AP mode is supported
 * @return WTERM_SUCCESS on success, error code otherwise
 */
wterm_result_t iw_check_ap_mode_support(const char *interface, bool *supports_ap);

/**
 * @brief Check if an interface supports 5GHz band
 * @param interface Interface name to check
 * @param supports_5ghz Output: true if 5GHz is supported
 * @return WTERM_SUCCESS on success, error code otherwise
 */
wterm_result_t iw_check_5ghz_support(const char *interface, bool *supports_5ghz);

/**
 * @brief Get the PHY index for a given interface
 * @param interface Interface name (e.g., "wlan0")
 * @param phy_index Output: PHY index number
 * @return WTERM_SUCCESS on success, error code otherwise
 */
wterm_result_t iw_get_phy_index(const char *interface, int *phy_index);

/**
 * @brief Check if WiFi interface is currently associated at kernel level
 * @param interface Interface name to check
 * @param is_associated Output: true if associated
 * @return WTERM_SUCCESS on success, error code otherwise
 */
wterm_result_t iw_check_association(const char *interface, bool *is_associated);

/**
 * @brief Get the SSID of currently connected network at kernel level
 * @param interface Interface name to check
 * @param ssid Output buffer for SSID (empty string if not connected)
 * @param ssid_size Size of SSID buffer
 * @return WTERM_SUCCESS on success, error code otherwise
 */
wterm_result_t iw_get_connected_ssid(const char *interface, char *ssid, size_t ssid_size);

/**
 * @brief Get link quality information for an interface
 * @param interface Interface name
 * @param signal_dbm Output: signal strength in dBm
 * @param tx_bitrate Output: TX bitrate in Mbps (0 if not available)
 * @param rx_bitrate Output: RX bitrate in Mbps (0 if not available)
 * @return WTERM_SUCCESS on success, error code otherwise
 */
wterm_result_t iw_get_link_quality(const char *interface, int *signal_dbm,
                                   int *tx_bitrate, int *rx_bitrate);

/**
 * @brief Check if iw command is available on the system
 * @return true if iw is available, false otherwise
 */
bool iw_is_available(void);

/**
 * @brief Check if an interface has an IPv4 address assigned
 * @param interface Interface name to check
 * @param has_ip Output: true if interface has IPv4 address
 * @return WTERM_SUCCESS on success, error code otherwise
 */
wterm_result_t interface_has_ip_address(const char *interface, bool *has_ip);

#endif // IW_HELPER_H
