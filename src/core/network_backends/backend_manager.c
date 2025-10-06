/**
 * @file backend_manager.c
 * @brief NetworkManager (nmcli) backend management
 */

#include "backend_interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global backend state
static const network_backend_t* current_backend = NULL;
static bool backend_initialized = false;

bool command_exists(const char* command) {
    if (!command) return false;

    char test_command[256];
    snprintf(test_command, sizeof(test_command), "command -v %s >/dev/null 2>&1", command);
    return (system(test_command) == 0);
}

network_manager_type_t detect_network_manager(void) {
    if (command_exists("nmcli")) {
        // Check if nmcli can actually work
        if (system("nmcli device status >/dev/null 2>&1") == 0) {
            return NETMGR_NMCLI;
        }
    }
    return NETMGR_UNKNOWN;
}

wterm_result_t init_network_backend(void) {
    if (backend_initialized && current_backend) {
        return WTERM_SUCCESS; // Already initialized
    }

    if (nmcli_backend.is_available()) {
        current_backend = &nmcli_backend;
        backend_initialized = true;
        return WTERM_SUCCESS;
    }

    current_backend = NULL;
    backend_initialized = false;
    return WTERM_ERROR_NETWORK;
}

const network_backend_t* get_current_backend(void) {
    if (!backend_initialized) {
        init_network_backend();
    }
    return current_backend;
}

network_manager_type_t get_backend_type(void) {
    const network_backend_t* backend = get_current_backend();
    return backend ? backend->type : NETMGR_UNKNOWN;
}