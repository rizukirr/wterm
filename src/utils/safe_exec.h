/**
 * @file safe_exec.h
 * @brief Safe command execution utilities
 *
 * Provides safer alternatives to system() that use fork/exec pattern
 * to avoid shell injection vulnerabilities.
 */

#ifndef WTERM_SAFE_EXEC_H
#define WTERM_SAFE_EXEC_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Execute a command safely without shell interpretation
 *
 * Uses fork/exec to run a command with arguments, avoiding shell injection.
 *
 * @param program Program name or path (argv[0])
 * @param args NULL-terminated array of arguments (including program name)
 * @return Exit code of the command (0 = success, non-zero = failure)
 */
int safe_exec_command(const char* program, char* const args[]);

/**
 * @brief Check if a command exists in PATH
 *
 * @param command Command name to check
 * @return true if command exists and is executable, false otherwise
 */
bool safe_command_exists(const char* command);

/**
 * @brief Execute a command and check if it succeeds (returns 0)
 *
 * @param program Program name or path
 * @param args NULL-terminated array of arguments
 * @return true if command succeeded (exit code 0), false otherwise
 */
bool safe_exec_check(const char* program, char* const args[]);

#endif // WTERM_SAFE_EXEC_H
