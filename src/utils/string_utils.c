/**
 * @file string_utils.c
 * @brief String manipulation utilities implementation
 */

#include "string_utils.h"
#include <string.h>
#include <ctype.h>

bool safe_string_copy(char *dest, const char *src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) {
        return false;
    }

    size_t src_len = strlen(src);
    if (src_len >= dest_size) {
        // String will be truncated
        strncpy(dest, src, dest_size - 1);
        dest[dest_size - 1] = '\0';
        return false;
    }

    strcpy(dest, src);
    return true;
}

void trim_trailing_whitespace(char *str) {
    if (!str) return;

    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}

const char *trim_leading_whitespace(const char *str) {
    if (!str) return NULL;

    while (isspace((unsigned char)*str)) {
        str++;
    }
    return str;
}

bool is_string_empty(const char *str) {
    if (!str) return true;

    while (*str) {
        if (!isspace((unsigned char)*str)) {
            return false;
        }
        str++;
    }
    return true;
}

const char *find_nth_char(const char *str, char ch, int n) {
    if (!str || n < 1) return NULL;

    int count = 0;
    while (*str) {
        if (*str == ch) {
            count++;
            if (count == n) {
                return str;
            }
        }
        str++;
    }
    return NULL;
}