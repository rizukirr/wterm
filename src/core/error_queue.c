/**
 * @file error_queue.c
 * @brief Thread-safe global error queue implementation
 */

#define _POSIX_C_SOURCE 200809L
#include "error_queue.h"
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// Maximum errors to queue (circular buffer)
#define ERROR_QUEUE_SIZE 32

// Maximum error message length
#define ERROR_MESSAGE_MAX 512

// Error queue entry
typedef struct {
    char message[ERROR_MESSAGE_MAX];
    bool is_error;  // true = error (red), false = warning (green)
} error_entry_t;

// Global error queue state
static struct {
    error_entry_t entries[ERROR_QUEUE_SIZE];
    int head;    // Next write position
    int tail;    // Next read position
    int count;   // Number of errors in queue
    pthread_mutex_t mutex;
    bool initialized;
} error_queue = {
    .head = 0,
    .tail = 0,
    .count = 0,
    .initialized = false
};

void error_queue_init(void) {
    if (error_queue.initialized) {
        return;  // Already initialized
    }

    // Initialize mutex
    pthread_mutex_init(&error_queue.mutex, NULL);

    // Reset queue state
    error_queue.head = 0;
    error_queue.tail = 0;
    error_queue.count = 0;

    error_queue.initialized = true;
}

void error_queue_cleanup(void) {
    if (!error_queue.initialized) {
        return;  // Not initialized, nothing to clean up
    }

    pthread_mutex_lock(&error_queue.mutex);

    // Clear queue
    error_queue.head = 0;
    error_queue.tail = 0;
    error_queue.count = 0;
    error_queue.initialized = false;

    pthread_mutex_unlock(&error_queue.mutex);

    // Destroy mutex
    pthread_mutex_destroy(&error_queue.mutex);
}

void error_queue_push(const char *message, bool is_error) {
    if (!message) {
        return;  // Invalid input
    }

    pthread_mutex_lock(&error_queue.mutex);

    if (!error_queue.initialized) {
        pthread_mutex_unlock(&error_queue.mutex);
        return;  // Queue not initialized (CLI mode)
    }

    // Copy message to current head position
    error_entry_t *entry = &error_queue.entries[error_queue.head];
    strncpy(entry->message, message, ERROR_MESSAGE_MAX - 1);
    entry->message[ERROR_MESSAGE_MAX - 1] = '\0';
    entry->is_error = is_error;

    // Advance head (circular)
    error_queue.head = (error_queue.head + 1) % ERROR_QUEUE_SIZE;

    // If queue is full, advance tail (discard oldest error)
    if (error_queue.count == ERROR_QUEUE_SIZE) {
        error_queue.tail = (error_queue.tail + 1) % ERROR_QUEUE_SIZE;
    } else {
        error_queue.count++;
    }

    pthread_mutex_unlock(&error_queue.mutex);
}

bool error_queue_has_errors(void) {
    pthread_mutex_lock(&error_queue.mutex);

    if (!error_queue.initialized) {
        pthread_mutex_unlock(&error_queue.mutex);
        return false;  // Queue not initialized
    }

    bool has_errors = (error_queue.count > 0);
    pthread_mutex_unlock(&error_queue.mutex);

    return has_errors;
}

bool error_queue_pop(char *message_out, size_t max_len, bool *is_error_out) {
    if (!message_out || max_len == 0) {
        return false;  // Invalid output buffer
    }

    pthread_mutex_lock(&error_queue.mutex);

    if (!error_queue.initialized) {
        pthread_mutex_unlock(&error_queue.mutex);
        return false;  // Queue not initialized
    }

    // Check if queue is empty
    if (error_queue.count == 0) {
        pthread_mutex_unlock(&error_queue.mutex);
        return false;
    }

    // Get error at tail position
    error_entry_t *entry = &error_queue.entries[error_queue.tail];

    // Copy message to output buffer
    strncpy(message_out, entry->message, max_len - 1);
    message_out[max_len - 1] = '\0';

    // Copy error flag if requested
    if (is_error_out) {
        *is_error_out = entry->is_error;
    }

    // Advance tail (circular)
    error_queue.tail = (error_queue.tail + 1) % ERROR_QUEUE_SIZE;
    error_queue.count--;

    pthread_mutex_unlock(&error_queue.mutex);

    return true;
}

void error_queue_clear(void) {
    pthread_mutex_lock(&error_queue.mutex);

    if (!error_queue.initialized) {
        pthread_mutex_unlock(&error_queue.mutex);
        return;  // Queue not initialized
    }

    // Reset queue to empty state
    error_queue.head = 0;
    error_queue.tail = 0;
    error_queue.count = 0;

    pthread_mutex_unlock(&error_queue.mutex);
}

void error_queue_push_formatted(bool is_error, const char *format, ...) {
    if (!format) {
        return;  // Invalid format string
    }

    char buffer[ERROR_MESSAGE_MAX];
    va_list args;

    // Format the message
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // Push to queue (will skip if not initialized)
    error_queue_push(buffer, is_error);
}
