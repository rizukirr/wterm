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
 * @brief Check if iw command is available on the system
 * @return true if iw is available, false otherwise
 */
bool iw_is_available(void);

#endif // IW_HELPER_H
