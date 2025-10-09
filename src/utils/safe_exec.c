/**
 * @file safe_exec.c
 * @brief Safe command execution implementation
 */

#define _POSIX_C_SOURCE 200809L
#include "safe_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

// cppcheck-suppress unusedFunction ; Exported API function
int safe_exec_command(const char* program, char* const args[]) {
    if (!program || !args) {
        return -1;
    }

    pid_t pid = fork();

    if (pid < 0) {
        // Fork failed
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        // Child process
        // Redirect stderr to /dev/null if needed
        execvp(program, args);

        // execvp only returns on error
        perror("execvp");
        _exit(127); // Command not found
    }

    // Parent process
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return -1;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    return -1; // Abnormal termination
}

bool safe_command_exists(const char* command) {
    if (!command) {
        return false;
    }

    // Use 'which' to check if command exists (which is an actual executable)
    char* const args[] = {
        "which",
        (char*)command,
        NULL
    };

    pid_t pid = fork();

    if (pid < 0) {
        return false;
    }

    if (pid == 0) {
        // Child process - redirect output to /dev/null
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        execvp("which", args);
        _exit(127);
    }

    // Parent process
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        return false;
    }

    return (WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

bool safe_exec_check(const char* program, char* const args[]) {
    int result = safe_exec_command(program, args);
    return (result == 0);
}

// cppcheck-suppress unusedFunction ; Exported API function
bool safe_exec_check_silent(const char* program, char* const args[]) {
    if (!program || !args) {
        return false;
    }

    pid_t pid = fork();

    if (pid < 0) {
        // Fork failed
        return false;
    }

    if (pid == 0) {
        // Child process - redirect stdout and stderr to /dev/null
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        execvp(program, args);
        _exit(127); // Command not found
    }

    // Parent process
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        return false;
    }

    return (WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
