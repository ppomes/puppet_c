/**
 * @file exec_utils.h
 * @brief Command execution utilities
 *
 * Provides common functions for executing shell commands
 * with output capture and error handling.
 */

#ifndef EXEC_UTILS_H
#define EXEC_UTILS_H

#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Escape a string for safe use inside a shell command
 *
 * Wraps the string in single quotes, escaping any embedded single quotes
 * using the '\'' technique. The returned string includes surrounding quotes.
 *
 * @param str String to escape (can be NULL)
 * @return Newly allocated escaped string (caller must free), or NULL on error
 */
char *shell_escape(const char *str);

/**
 * @brief Run a command and capture its output
 *
 * @param cmd Command to execute (passed to popen)
 * @param output Buffer to store command output (can be NULL)
 * @param output_size Size of output buffer
 * @return Exit code of command, or -1 on error
 */
int run_command(const char *cmd, char *output, size_t output_size);

/**
 * @brief Execute a command with optional verbose logging
 *
 * @param cmd Command to execute (passed to system)
 * @param verbose If true, print command to stderr before execution
 * @return Return value from system()
 */
int exec_command(const char *cmd, bool verbose);

/**
 * @brief Run a command and return its output as allocated string
 *
 * @param cmd Command to execute
 * @return Newly allocated string with output (caller must free), or NULL on error
 */
char *run_command_output(const char *cmd);

#endif /* EXEC_UTILS_H */
