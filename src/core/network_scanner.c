/**
 * @file network_scanner.c
 * @brief WiFi network scanning and management implementation using backend system
 */

#define _POSIX_C_SOURCE 200809L
#include "network_scanner.h"
#include "network_backends/backend_interface.h"
#include "../utils/string_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

wterm_result_t parse_network_line(const char *buffer, network_info_t *network) {
  if (!buffer || !network) {
    return WTERM_ERROR_INVALID_INPUT;
  }

  // Find colon positions
  const char *first_colon = strchr(buffer, ':');
  const char *second_colon = first_colon ? strchr(first_colon + 1, ':') : NULL;

  if (!first_colon || !second_colon) {
    return WTERM_ERROR_PARSE;
  }

  // Calculate field lengths
  size_t ssid_len = first_colon - buffer;
  size_t security_len = second_colon - (first_colon + 1);
  const char *signal = second_colon + 1;

  // Clear the network structure
  memset(network, 0, sizeof(network_info_t));

  // Copy SSID with bounds checking
  if (ssid_len >= MAX_STR_SSID) {
    ssid_len = MAX_STR_SSID - 1;
  }
  strncpy(network->ssid, buffer, ssid_len);
  network->ssid[ssid_len] = '\0';

  // Copy security field or set "Open" if empty
  if (security_len > 0) {
    if (security_len >= MAX_STR_SECURITY) {
      security_len = MAX_STR_SECURITY - 1;
    }
    strncpy(network->security, first_colon + 1, security_len);
    network->security[security_len] = '\0';
  } else {
    safe_string_copy(network->security, "Open", MAX_STR_SECURITY);
  }

  // Copy signal with bounds checking
  safe_string_copy(network->signal, signal, MAX_STR_SIGNAL);

  // Trim whitespace from all fields
  trim_trailing_whitespace(network->ssid);
  trim_trailing_whitespace(network->security);
  trim_trailing_whitespace(network->signal);

  return WTERM_SUCCESS;
}

wterm_result_t scan_wifi_networks(network_list_t *network_list) {
  if (!network_list) {
    return WTERM_ERROR_INVALID_INPUT;
  }

  // Initialize network list
  network_list->count = 0;
  memset(network_list->networks, 0, sizeof(network_list->networks));

  // Get the current backend
  const network_backend_t* backend = get_current_backend();
  if (!backend) {
    fprintf(stderr, "No supported network manager found. Please install NetworkManager (nmcli) or iwd (iwctl).\n");
    return WTERM_ERROR_NETWORK;
  }

  // Use backend to scan networks
  wterm_result_t result = backend->scan_networks(network_list);
  if (result != WTERM_SUCCESS) {
    fprintf(stderr, "Failed to scan networks using %s\n", backend->name);
    return result;
  }

  return WTERM_SUCCESS;
}

void display_networks(const network_list_t *network_list) {
  if (!network_list) {
    printf("No network list provided\n");
    return;
  }

  printf("Found %d Wi-Fi networks:\n\n", network_list->count);

  if (network_list->count == 0) {
    printf("No networks found. Try running a rescan.\n");
    return;
  }

  // Print header
  printf("%-32s | %-16s | %s\n", "SSID", "Security", "Signal");
  printf("%s\n",
         "----------------------------------------------------------------");

  // Print each network
  for (int i = 0; i < network_list->count; i++) {
    const network_info_t *network = &network_list->networks[i];
    printf("%-32s | %-16s | %s\n", network->ssid, network->security,
           network->signal);
  }
}

wterm_result_t rescan_wifi_networks(void) {
  return rescan_wifi_networks_silent(false);
}

wterm_result_t rescan_wifi_networks_silent(bool silent) {
  if (!silent) {
    printf("Rescanning WiFi networks...\n");
  }

  // Get the current backend
  const network_backend_t* backend = get_current_backend();
  if (!backend) {
    if (!silent) {
      fprintf(stderr, "No supported network manager found. Please install NetworkManager (nmcli) or iwd (iwctl).\n");
    }
    return WTERM_ERROR_NETWORK;
  }

  // Use backend to rescan
  wterm_result_t result = backend->rescan_networks();
  if (result != WTERM_SUCCESS) {
    if (!silent) {
      fprintf(stderr, "Failed to trigger WiFi rescan using %s\n", backend->name);
    }
    return result;
  }

  if (!silent) {
    printf("Scan initiated using %s. Please wait a moment before checking results.\n", backend->name);
  }
  return WTERM_SUCCESS;
}
