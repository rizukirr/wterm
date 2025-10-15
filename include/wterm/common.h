#pragma once

/**
 * @file common.h
 * @brief Common definitions and constants for wterm project
 */

#include <stddef.h>
#include <stdbool.h>

// Version information
#define WTERM_VERSION_MAJOR 2
#define WTERM_VERSION_MINOR 0
#define WTERM_VERSION_PATCH 0
#define WTERM_VERSION_STRING "2.0.0"

// Network configuration limits
#define MAX_STR_SSID 33         // 32 bytes SSID + '\0'
#define MAX_STR_SECURITY 17     // long enough for WPA2-Enterprise
#define MAX_STR_SIGNAL 33       // flexible signal representation
#define MAX_NETWORKS 32         // handle up to 32 available networks

// Hotspot configuration limits
#define MAX_STR_INTERFACE 16    // network interface name (e.g., wlan0)
#define MAX_STR_PASSWORD 64     // WPA password or PSK
#define MAX_STR_IP_ADDR 16      // IPv4 address (xxx.xxx.xxx.xxx + '\0')
#define MAX_STR_DEVICE_NAME 32  // connected device hostname
#define MAX_STR_MAC_ADDR 18     // MAC address (aa:bb:cc:dd:ee:ff + '\0')
#define MAX_HOTSPOT_CLIENTS 32  // maximum connected clients
#define MAX_HOTSPOTS 16         // maximum saved hotspot configurations

// Command definitions - moved to backend implementations
// Legacy defines kept for backward compatibility with get_networks.c
#define CMD_WIFI_LIST "nmcli -t -f SSID,SECURITY,SIGNAL device wifi list"

// Return codes
typedef enum {
    WTERM_SUCCESS = 0,
    WTERM_ERROR_GENERAL = 1,
    WTERM_ERROR_NETWORK = 2,
    WTERM_ERROR_PARSE = 3,
    WTERM_ERROR_MEMORY = 4,
    WTERM_ERROR_INVALID_INPUT = 5,
    WTERM_ERROR_HOTSPOT = 6,
    WTERM_ERROR_INTERFACE = 7,
    WTERM_ERROR_PERMISSION = 8,
    WTERM_ERROR_CANCELLED = 9
} wterm_result_t;

// WiFi security types
typedef enum {
    WIFI_SECURITY_NONE = 0,
    WIFI_SECURITY_WEP,
    WIFI_SECURITY_WPA,
    WIFI_SECURITY_WPA2,
    WIFI_SECURITY_WPA3,
    WIFI_SECURITY_WPA_WPA2,
    WIFI_SECURITY_ENTERPRISE
} wifi_security_t;

// Hotspot sharing methods
typedef enum {
    HOTSPOT_SHARE_NONE = 0,    // No internet sharing
    HOTSPOT_SHARE_NAT,         // NAT-based sharing (default)
    HOTSPOT_SHARE_BRIDGE       // Bridge mode sharing
} hotspot_share_method_t;

// Hotspot state enumeration
typedef enum {
    HOTSPOT_STATE_STOPPED = 0,
    HOTSPOT_STATE_STARTING,
    HOTSPOT_STATE_ACTIVE,
    HOTSPOT_STATE_STOPPING,
    HOTSPOT_STATE_ERROR
} hotspot_state_t;

// Network information structure
typedef struct {
    char ssid[MAX_STR_SSID];
    char security[MAX_STR_SECURITY];
    char signal[MAX_STR_SIGNAL];
} network_info_t;

// Network list structure
typedef struct {
    network_info_t networks[MAX_NETWORKS];
    int count;
} network_list_t;

// Connected device information
typedef struct {
    char hostname[MAX_STR_DEVICE_NAME];
    char ip_address[MAX_STR_IP_ADDR];
    char mac_address[MAX_STR_MAC_ADDR];
    bool is_connected;
    unsigned long bytes_sent;
    unsigned long bytes_received;
} hotspot_client_t;

// Hotspot configuration structure
typedef struct {
    char name[MAX_STR_SSID];              // Hotspot profile name
    char ssid[MAX_STR_SSID];              // Network SSID
    char password[MAX_STR_PASSWORD];       // Network password
    char wifi_interface[MAX_STR_INTERFACE]; // WiFi interface (e.g., wlan0)
    char internet_interface[MAX_STR_INTERFACE]; // Internet source interface
    char gateway_ip[MAX_STR_IP_ADDR];     // Gateway IP address
    wifi_security_t security_type;         // Security protocol
    hotspot_share_method_t share_method;   // Internet sharing method
    int channel;                          // WiFi channel (1-14 for 2.4GHz)
    bool hidden;                          // Hide SSID broadcast
    bool client_isolation;                // Isolate clients from each other
    bool mac_filtering;                   // Enable MAC address filtering
    bool is_5ghz;                        // Use 5GHz band
    char virtual_interface[MAX_STR_INTERFACE]; // Virtual interface name (if using concurrent mode)
    bool use_virtual_if_possible;         // Prefer virtual interface for WiFi-to-WiFi sharing
} hotspot_config_t;

// Hotspot runtime status
typedef struct {
    hotspot_config_t config;              // Configuration used
    hotspot_state_t state;                // Current state
    char status_message[256];             // Status/error message
    int client_count;                     // Number of connected clients
    hotspot_client_t clients[MAX_HOTSPOT_CLIENTS]; // Connected devices
    unsigned long uptime_seconds;         // Hotspot uptime
    unsigned long total_bytes_sent;       // Total data sent
    unsigned long total_bytes_received;   // Total data received
    int process_id;                       // Hotspot process PID
    bool is_persistent;                   // Save configuration
} hotspot_status_t;

// Hotspot list structure
typedef struct {
    hotspot_config_t hotspots[MAX_HOTSPOTS];
    int count;
} hotspot_list_t;

// Interface information for hotspot UI
typedef struct {
    char name[MAX_STR_INTERFACE];      // Interface name (e.g., wlan0)
    char status[32];                   // Status: connected, disconnected
    bool supports_ap;                  // Supports Access Point mode
    bool supports_concurrent;          // Supports concurrent client+AP mode
    bool is_connected;                 // Currently connected to a network
    int current_channel;               // Current channel if connected (-1 if not)
} interface_info_t;

// Band option for hotspot UI
typedef struct {
    char id[8];                        // Band ID: "bg", "a"
    char display[64];                  // Display text for user
} band_option_t;

// Security option for hotspot UI
typedef struct {
    char id[16];                       // Security ID: "secured", "open"
    char display[64];                  // Display text for user
} security_option_t;

