#pragma once

/**
 * @file backend_interface.h
 * @brief Network manager backend interface for wterm
 *
 * This header defines the interface for NetworkManager (nmcli) backend.
 */

#include "wterm/common.h"

// Network manager backend types
typedef enum {
    NETMGR_UNKNOWN = 0,
    NETMGR_NMCLI         // NetworkManager (nmcli)
} network_manager_type_t;

// Connection result for backend operations
typedef struct {
    wterm_result_t result;
    char error_message[256];
} backend_result_t;

// Network backend interface
typedef struct {
    // Backend identification
    network_manager_type_t type;
    const char* name;
    const char* command;

    // Core operations
    wterm_result_t (*scan_networks)(network_list_t* networks);
    backend_result_t (*connect_open_network)(const char* ssid);
    backend_result_t (*connect_secured_network)(const char* ssid, const char* password);
    backend_result_t (*disconnect_network)(void);
    wterm_result_t (*rescan_networks)(void);

    // Availability check
    bool (*is_available)(void);

    // Status operations
    bool (*is_connected)(char* connected_ssid, size_t buffer_size);
    bool (*get_ip_address)(char* ip_buffer, size_t buffer_size);

    // Hotspot operations
    backend_result_t (*create_hotspot)(const hotspot_config_t* config);
    backend_result_t (*start_hotspot)(const char* hotspot_name);
    backend_result_t (*stop_hotspot)(const char* hotspot_name);
    backend_result_t (*delete_hotspot)(const char* hotspot_name);
    wterm_result_t (*get_hotspot_status)(const char* hotspot_name, hotspot_status_t* status);
    wterm_result_t (*list_active_hotspots)(char active_hotspots[][MAX_STR_SSID], int max_count, int* count);
    wterm_result_t (*get_hotspot_clients)(const char* hotspot_name, hotspot_client_t* clients, int max_clients, int* client_count);

    // Interface capability checking
    wterm_result_t (*check_interface_ap_support)(const char* interface, bool* supports_ap);
    wterm_result_t (*get_available_wifi_interfaces)(char interfaces[][MAX_STR_INTERFACE], int max_interfaces, int* interface_count);
} network_backend_t;

// Backend management functions

/**
 * @brief Initialize the network backend system
 * @return WTERM_SUCCESS if a backend was found and initialized
 */
wterm_result_t init_network_backend(void);

/**
 * @brief Get the currently active network backend
 * @return Pointer to active backend, or NULL if none initialized
 */
const network_backend_t* get_current_backend(void);

/**
 * @brief Get the type of the currently active backend
 * @return Backend type, or NETMGR_UNKNOWN if none initialized
 */
network_manager_type_t get_backend_type(void);

/**
 * @brief Check if a specific command exists on the system
 * @param command Command to check (e.g., "nmcli", "iwctl")
 * @return true if command is available
 */
bool command_exists(const char* command);

/**
 * @brief Detect available network managers on the system
 * @return Type of the preferred available network manager
 */
network_manager_type_t detect_network_manager(void);

// Backend implementation
extern const network_backend_t nmcli_backend;