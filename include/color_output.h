/**
 * @file color_output.h
 * @brief Colorized terminal output utilities
 *
 * Provides Puppet-style colored output for the agent.
 * Colors are automatically disabled when output is not a TTY.
 */

#ifndef COLOR_OUTPUT_H
#define COLOR_OUTPUT_H

#include <stdbool.h>

/* ANSI color codes */
#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_RED     "\033[31m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_BLUE    "\033[34m"
#define ANSI_MAGENTA "\033[35m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_WHITE   "\033[37m"

/* Bold color combinations */
#define ANSI_BOLD_RED     "\033[1;31m"
#define ANSI_BOLD_GREEN   "\033[1;32m"
#define ANSI_BOLD_YELLOW  "\033[1;33m"
#define ANSI_BOLD_CYAN    "\033[1;36m"

/**
 * @brief Initialize color output (call once at startup)
 *
 * Detects if stdout is a TTY and enables/disables colors accordingly.
 */
void color_init(void);

/**
 * @brief Force enable or disable colors
 */
void color_set_enabled(bool enabled);

/**
 * @brief Check if colors are enabled
 */
bool color_is_enabled(void);

/**
 * @brief Print an info message (cyan "Info:")
 */
void print_info(const char *fmt, ...);

/**
 * @brief Print a notice message (green "Notice:")
 */
void print_notice(const char *fmt, ...);

/**
 * @brief Print a warning message (yellow "Warning:")
 */
void print_warning(const char *fmt, ...);

/**
 * @brief Print an error message (red "Error:")
 */
void print_error(const char *fmt, ...);

/**
 * @brief Print a debug message (magenta "Debug:")
 */
void print_debug(const char *fmt, ...);

/**
 * @brief Print a resource change in Puppet format
 *
 * Format: Notice: /Stage[main]/Main/Type[title]/attribute: message
 *
 * @param type Resource type (e.g., "File", "Package")
 * @param title Resource title (e.g., "/tmp/test")
 * @param attribute Attribute being changed (e.g., "ensure", "content")
 * @param message Description of the change
 */
void print_resource_change(const char *type, const char *title,
                          const char *attribute, const char *fmt, ...);

/**
 * @brief Print a resource status (for noop mode)
 *
 * Format: Notice: /Stage[main]/Main/Type[title]/attribute: current_value would be changed to new_value (noop)
 */
void print_resource_noop(const char *type, const char *title,
                        const char *attribute, const char *message);

/**
 * @brief Print a resource error
 *
 * Format: Error: /Stage[main]/Main/Type[title]: error message
 */
void print_resource_error(const char *type, const char *title,
                         const char *fmt, ...);

/**
 * @brief Print bold text
 */
void print_bold(const char *fmt, ...);

/**
 * @brief Print colored text
 */
void print_color(const char *color, const char *fmt, ...);

#endif /* COLOR_OUTPUT_H */
