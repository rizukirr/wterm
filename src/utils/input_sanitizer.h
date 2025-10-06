#pragma once

/**
 * @file input_sanitizer.h
 * @brief Input sanitization and validation for security
 *
 * Provides functions to sanitize and validate user inputs to prevent
 * command injection, format string attacks, and other security issues.
 */

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Escape shell special characters for safe command execution
 *
 * Escapes characters that have special meaning in shell: $, `, \, ", ', etc.
 * Uses single quotes for safety and escapes any single quotes in the input.
 *
 * @param input Input string to escape
 * @param output Buffer to store escaped string
 * @param output_size Size of output buffer
 * @return true if successful, false if buffer too small or invalid input
 */
bool shell_escape(const char *input, char *output, size_t output_size);

/**
 * @brief Validate that string contains only safe characters for shell use
 *
 * Checks if input contains only alphanumeric, space, dash, underscore, and dot.
 * Useful for validating interface names, SSIDs that will be used in commands.
 *
 * @param input String to validate
 * @return true if string is safe, false otherwise
 */
bool is_shell_safe(const char *input);

/**
 * @brief Validate WiFi SSID for safe use
 *
 * Checks SSID length (1-32 bytes) and ensures it doesn't contain
 * dangerous shell metacharacters if it will be used in commands.
 *
 * @param ssid SSID to validate
 * @return true if valid and safe, false otherwise
 */
bool validate_ssid(const char *ssid);

/**
 * @brief Validate network interface name
 *
 * Ensures interface name contains only safe characters (alphanumeric, dash)
 * and has reasonable length (1-15 characters).
 *
 * @param interface Interface name to validate
 * @return true if valid, false otherwise
 */
bool validate_interface_name(const char *interface);

/**
 * @brief Validate hotspot name for NetworkManager
 *
 * Ensures connection name is safe for use in nmcli commands.
 * Allows alphanumeric, dash, underscore (1-64 characters).
 *
 * @param name Hotspot/connection name to validate
 * @return true if valid, false otherwise
 */
bool validate_hotspot_name(const char *name);

/**
 * @brief Sanitize string by removing/replacing dangerous characters
 *
 * Creates a sanitized version by replacing dangerous characters with underscores.
 * Useful when you need to accept arbitrary input but use it safely.
 *
 * @param input Input string
 * @param output Buffer for sanitized string
 * @param output_size Size of output buffer
 * @return true if successful, false if buffer too small
 */
bool sanitize_string(const char *input, char *output, size_t output_size);

/**
 * @brief Check if string contains format string specifiers
 *
 * Detects potential format string vulnerabilities by checking for %s, %x, etc.
 *
 * @param input String to check
 * @return true if format specifiers found, false otherwise
 */
bool contains_format_specifiers(const char *input);
