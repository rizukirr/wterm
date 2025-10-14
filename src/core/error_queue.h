#pragma once

/**
 * @file error_queue.h
 * @brief Thread-safe global error queue for displaying errors in TUI popups
 *
 * This module provides a centralized error handling system that:
 * - Captures error messages from anywhere in the codebase
 * - Stores them in a thread-safe queue
 * - Allows TUI to poll and display errors in modal dialogs
 * - Maintains stderr printing for CLI mode compatibility
 */

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Initialize the global error queue
 *
 * Must be called once at application startup before using error queue.
 * Safe to call multiple times - subsequent calls are ignored.
 */
void error_queue_init(void);

/**
 * @brief Cleanup the global error queue
 *
 * Must be called at application shutdown to free resources.
 * Safe to call even if not initialized.
 */
void error_queue_cleanup(void);

/**
 * @brief Push an error message to the queue
 *
 * Thread-safe. Can be called from any thread including background connection threads.
 * If queue is full, oldest error is discarded.
 *
 * @param message Error message (will be truncated to 511 chars if too long)
 * @param is_error true for errors (red), false for warnings/info (green)
 */
void error_queue_push(const char *message, bool is_error);

/**
 * @brief Check if queue has pending errors
 *
 * Thread-safe. Use this to check before popping.
 *
 * @return true if at least one error is queued, false if empty
 */
bool error_queue_has_errors(void);

/**
 * @brief Pop the oldest error from the queue
 *
 * Thread-safe. Removes and returns the oldest error message.
 *
 * @param message_out Buffer to receive error message
 * @param max_len Maximum length of message_out buffer
 * @param is_error_out Output: true if error, false if warning/info (can be NULL)
 * @return true if error was popped, false if queue was empty
 */
bool error_queue_pop(char *message_out, size_t max_len, bool *is_error_out);

/**
 * @brief Clear all errors from the queue
 *
 * Thread-safe. Useful for CLI mode to discard errors after display.
 */
void error_queue_clear(void);

/**
 * @brief Internal function to push formatted error (used by REPORT_ERROR macro)
 *
 * Do not call directly - use REPORT_ERROR macro instead.
 *
 * @param is_error true for errors, false for warnings
 * @param format printf-style format string
 * @param ... Variable arguments for format string
 */
void error_queue_push_formatted(bool is_error, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

/**
 * @brief Error reporting macro - replaces fprintf(stderr, ...)
 *
 * Usage:
 *   REPORT_ERROR(true, "Failed to start hotspot: %s", reason);
 *   REPORT_ERROR(false, "Warning: Could not verify 5GHz support");
 *
 * Features:
 * - Prints to stderr for CLI mode compatibility
 * - Queues error for TUI display if error queue is initialized
 * - Thread-safe
 * - Works from any context
 *
 * @param is_error true for errors (red in TUI), false for warnings (green in TUI)
 * @param format printf-style format string
 * @param ... Variable arguments
 */
#define REPORT_ERROR(is_error, format, ...) \
    do { \
        fprintf(stderr, format "\n", ##__VA_ARGS__); \
        error_queue_push_formatted(is_error, format, ##__VA_ARGS__); \
    } while (0)
