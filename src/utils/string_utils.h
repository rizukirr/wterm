#pragma once

/**
 * @file string_utils.h
 * @brief String manipulation utilities for wterm project
 */

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Safely copy string with bounds checking
 * @param dest Destination buffer
 * @param src Source string
 * @param dest_size Size of destination buffer
 * @return true if copy was successful, false if truncated
 */
bool safe_string_copy(char *dest, const char *src, size_t dest_size);

/**
 * @brief Remove trailing whitespace from string
 * @param str String to trim (modified in place)
 */
void trim_trailing_whitespace(char *str);

/**
 * @brief Remove leading whitespace from string
 * @param str String to trim
 * @return Pointer to first non-whitespace character
 */
const char *trim_leading_whitespace(const char *str);

/**
 * @brief Check if string is empty or contains only whitespace
 * @param str String to check
 * @return true if string is empty/whitespace only
 */
bool is_string_empty(const char *str);

/**
 * @brief Find nth occurrence of character in string
 * @param str String to search
 * @param ch Character to find
 * @param n Which occurrence to find (1-based)
 * @return Pointer to nth occurrence, or NULL if not found
 */
const char *find_nth_char(const char *str, char ch, int n);
