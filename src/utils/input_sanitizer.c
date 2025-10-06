/**
 * @file input_sanitizer.c
 * @brief Input sanitization and validation implementation
 */

#include "input_sanitizer.h"
#include "string_utils.h"
#include <string.h>
#include <ctype.h>

bool shell_escape(const char *input, char *output, size_t output_size) {
    if (!input || !output || output_size < 3) {
        return false;
    }

    size_t input_len = strlen(input);
    size_t output_pos = 0;

    // Use single quotes for shell safety: 'text'
    // To include a single quote, we use: '\''
    // This closes the quote, adds escaped quote, reopens quote

    // Add opening single quote
    if (output_pos >= output_size - 1) return false;
    output[output_pos++] = '\'';

    for (size_t i = 0; i < input_len; i++) {
        if (input[i] == '\'') {
            // Need 4 chars: '\'' (close quote, escaped quote, open quote)
            if (output_pos + 4 >= output_size - 1) return false;
            output[output_pos++] = '\'';
            output[output_pos++] = '\\';
            output[output_pos++] = '\'';
            output[output_pos++] = '\'';
        } else {
            if (output_pos >= output_size - 1) return false;
            output[output_pos++] = input[i];
        }
    }

    // Add closing single quote
    if (output_pos >= output_size - 1) return false;
    output[output_pos++] = '\'';
    output[output_pos] = '\0';

    return true;
}

bool is_shell_safe(const char *input) {
    if (!input) return false;

    // Allow only alphanumeric, space, dash, underscore, dot
    for (size_t i = 0; input[i] != '\0'; i++) {
        char c = input[i];
        if (!isalnum((unsigned char)c) &&
            c != ' ' && c != '-' && c != '_' && c != '.') {
            return false;
        }
    }

    return strlen(input) > 0;
}

bool validate_ssid(const char *ssid) {
    if (!ssid) return false;

    size_t len = strlen(ssid);

    // SSID must be 1-32 bytes (not necessarily characters due to UTF-8)
    if (len == 0 || len > 32) {
        return false;
    }

    // Check for null bytes (SSIDs can technically contain them but we reject for safety)
    for (size_t i = 0; i < len; i++) {
        if (ssid[i] == '\0') {
            return false;
        }
    }

    return true;
}

bool validate_interface_name(const char *interface) {
    if (!interface) return false;

    size_t len = strlen(interface);

    // Interface names: 1-15 characters, alphanumeric + dash/underscore
    if (len == 0 || len > 15) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        char c = interface[i];
        if (!isalnum((unsigned char)c) && c != '-' && c != '_') {
            return false;
        }
    }

    // Cannot start with dash
    if (interface[0] == '-') {
        return false;
    }

    return true;
}

bool validate_hotspot_name(const char *name) {
    if (!name) return false;

    size_t len = strlen(name);

    // Connection names: 1-64 characters
    if (len == 0 || len > 64) {
        return false;
    }

    // Allow alphanumeric, dash, underscore, dot
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (!isalnum((unsigned char)c) && c != '-' && c != '_' && c != '.') {
            return false;
        }
    }

    return true;
}

bool sanitize_string(const char *input, char *output, size_t output_size) {
    if (!input || !output || output_size == 0) {
        return false;
    }

    size_t input_len = strlen(input);
    if (input_len >= output_size) {
        input_len = output_size - 1;
    }

    for (size_t i = 0; i < input_len; i++) {
        char c = input[i];
        // Keep alphanumeric, space, dash, underscore, dot
        if (isalnum((unsigned char)c) || c == ' ' ||
            c == '-' || c == '_' || c == '.') {
            output[i] = c;
        } else {
            // Replace dangerous characters with underscore
            output[i] = '_';
        }
    }

    output[input_len] = '\0';
    return true;
}

bool contains_format_specifiers(const char *input) {
    if (!input) return false;

    // Look for % followed by format specifier characters
    for (size_t i = 0; input[i] != '\0'; i++) {
        if (input[i] == '%') {
            // Check next character
            if (input[i + 1] != '\0') {
                char next = input[i + 1];
                // Common format specifiers: s, d, i, u, x, X, p, f, c, n
                if (next == 's' || next == 'd' || next == 'i' || next == 'u' ||
                    next == 'x' || next == 'X' || next == 'p' || next == 'f' ||
                    next == 'c' || next == 'n' || next == '%') {
                    return true;
                }
            }
        }
    }

    return false;
}
