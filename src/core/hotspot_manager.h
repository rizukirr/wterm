#pragma once

/**
 * @file hotspot_manager.h
 * @brief WiFi hotspot management functionality for wterm
 */

#include "../../include/wterm/common.h"
#include <stdbool.h>

/**
 * @brief Initialize the hotspot manager
 * @return WTERM_SUCCESS on success, error code on failure
 */
wterm_result_t hotspot_manager_init(void);

/**
 * @brief Cleanup hotspot manager resources
 */
void hotspot_manager_cleanup(void);

/**
 * @brief Create a new hotspot configuration
 * @param config Hotspot configuration to create
 * @return WTERM_SUCCESS on success, error code on failure
 */
wterm_result_t hotspot_create_config(const hotspot_config_t *config);

/**
 * @brief Start a hotspot by name
 * @param name Hotspot configuration name
 * @param status Output status information (optional, can be NULL)
 * @return WTERM_SUCCESS on success, error code on failure
 */
wterm_result_t hotspot_start(const char *name, hotspot_status_t *status);

/**
 * @brief Stop a running hotspot
 * @param name Hotspot name or NULL to stop all
 * @return WTERM_SUCCESS on success, error code on failure
 */
wterm_result_t hotspot_stop(const char *name);

/**
 * @brief Get hotspot status
 * @param name Hotspot name
 * @param status Output status structure
 * @return WTERM_SUCCESS on success, error code on failure
 */
wterm_result_t hotspot_get_status(const char *name, hotspot_status_t *status);

/**
 * @brief List all configured hotspots
 * @param list Output hotspot list
 * @return WTERM_SUCCESS on success, error code on failure
 */
wterm_result_t hotspot_list_configs(hotspot_list_t *list);

/**
 * @brief Delete a hotspot configuration
 * @param name Hotspot name to delete
 * @return WTERM_SUCCESS on success, error code on failure
 */
wterm_result_t hotspot_delete_config(const char *name);

/**
 * @brief Update an existing hotspot configuration
 * @param name Hotspot name to update
 * @param config New configuration
 * @return WTERM_SUCCESS on success, error code on failure
 */
wterm_result_t hotspot_update_config(const char *name,
                                     const hotspot_config_t *config);

/**
 * @brief Get list of connected clients for a hotspot
 * @param name Hotspot name
 * @param clients Output array of client information
 * @param max_clients Maximum number of clients to return
 * @param client_count Output number of clients found
 * @return WTERM_SUCCESS on success, error code on failure
 */
wterm_result_t hotspot_get_clients(const char *name, hotspot_client_t *clients,
                                   int max_clients, int *client_count);

/**
 * @brief Validate hotspot configuration
 * @param config Configuration to validate
 * @param error_msg Buffer for error message (can be NULL)
 * @param error_msg_size Size of error message buffer
 * @return WTERM_SUCCESS if valid, error code otherwise
 */
wterm_result_t hotspot_validate_config(const hotspot_config_t *config,
                                       char *error_msg, size_t error_msg_size);

/**
 * @brief Check if an interface supports hotspot mode
 * @param interface Interface name (e.g., "wlan0")
 * @param supports_ap Output flag for AP mode support
 * @param supports_monitor Output flag for monitor mode support
 * @return WTERM_SUCCESS on success, error code on failure
 */
wterm_result_t hotspot_check_interface_capabilities(const char *interface,
                                                    bool *supports_ap,
                                                    bool *supports_monitor);

/**
 * @brief Get available WiFi interfaces for hotspot use
 * @param interfaces Output array of interface names
 * @param max_interfaces Maximum number of interfaces to return
 * @param interface_count Output number of interfaces found
 * @return WTERM_SUCCESS on success, error code on failure
 */
wterm_result_t
hotspot_get_available_interfaces(char interfaces[][MAX_STR_INTERFACE],
                                 int max_interfaces, int *interface_count);

/**
 * @brief Quick hotspot creation with default settings
 * @param ssid Network name
 * @param password Network password (can be NULL for open network)
 * @param wifi_interface WiFi interface to use
 * @param internet_interface Internet source interface (can be NULL for no
 * sharing)
 * @param status Output status information
 * @return WTERM_SUCCESS on success, error code on failure
 */
wterm_result_t hotspot_quick_start(const char *ssid, const char *password,
                                   const char *wifi_interface,
                                   const char *internet_interface,
                                   hotspot_status_t *status);

/**
 * @brief Save hotspot configuration to persistent storage
 * @param config Configuration to save
 * @return WTERM_SUCCESS on success, error code on failure
 */
wterm_result_t hotspot_save_config_to_file(const hotspot_config_t *config);

/**
 * @brief Load hotspot configuration from persistent storage
 * @param name Configuration name
 * @param config Output configuration structure
 * @return WTERM_SUCCESS on success, error code on failure
 */
wterm_result_t hotspot_load_config_from_file(const char *name,
                                             hotspot_config_t *config);

/**
 * @brief Get default hotspot configuration
 * @param config Output configuration with default values
 */
void hotspot_get_default_config(hotspot_config_t *config);

/**
 * @brief Convert security type enum to string
 * @param security_type Security type enum
 * @return String representation of security type
 */
const char *hotspot_security_type_to_string(wifi_security_t security_type);

/**
 * @brief Convert security type string to enum
 * @param security_string String representation
 * @return Security type enum
 */
wifi_security_t hotspot_security_type_from_string(const char *security_string);

/**
 * @brief Convert share method enum to string
 * @param share_method Share method enum
 * @return String representation of share method
 */
const char *hotspot_share_method_to_string(hotspot_share_method_t share_method);

/**
 * @brief Convert share method string to enum
 * @param method_string String representation
 * @return Share method enum
 */
hotspot_share_method_t
hotspot_share_method_from_string(const char *method_string);

/**
 * @brief Get list of available WiFi interfaces with their capabilities
 * @param interfaces Output array of interface information
 * @param max_count Maximum number of interfaces to return
 * @param count Output number of interfaces found
 * @return WTERM_SUCCESS on success, error code on failure
 */
wterm_result_t hotspot_get_interface_list(interface_info_t *interfaces,
                                          int max_count, int *count);

/**
 * @brief Get available frequency band options
 * @param bands Output array of band options
 * @param count Output number of band options
 * @return WTERM_SUCCESS on success, error code on failure
 */
wterm_result_t hotspot_get_band_options(band_option_t *bands, int *count);

/**
 * @brief Get available security options
 * @param options Output array of security options
 * @param count Output number of security options
 * @return WTERM_SUCCESS on success, error code on failure
 */
wterm_result_t hotspot_get_security_options(security_option_t *options, int *count);

/**
 * @brief Create a virtual WiFi interface for concurrent client+AP operation
 * @param physical_interface Physical interface name (e.g., "wlan0")
 * @param virtual_interface Output buffer for virtual interface name (e.g., "vwlan0")
 * @param virtual_interface_size Size of output buffer
 * @return WTERM_SUCCESS on success, error code on failure
 */
wterm_result_t create_virtual_interface(const char *physical_interface,
                                       char *virtual_interface,
                                       size_t virtual_interface_size);

/**
 * @brief Delete a virtual WiFi interface
 * @param virtual_interface Virtual interface name to delete
 * @return WTERM_SUCCESS on success, error code on failure
 */
wterm_result_t delete_virtual_interface(const char *virtual_interface);

/**
 * @brief Check if an interface name refers to a virtual interface
 * @param interface Interface name to check
 * @return true if virtual, false if physical
 */
bool is_virtual_interface(const char *interface);
