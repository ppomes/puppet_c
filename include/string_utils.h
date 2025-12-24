/**
 * @file string_utils.h
 * @brief String manipulation utilities
 *
 * Provides common string helper functions.
 */

#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <stddef.h>

/**
 * @brief Safe strdup that handles NULL input
 *
 * @param s String to duplicate (can be NULL)
 * @return Duplicated string or NULL if input was NULL
 */
char *safe_strdup(const char *s);

/**
 * @brief Trim leading and trailing whitespace in place
 *
 * @param str String to trim (modified in place)
 * @return Pointer to trimmed string (may be different from input)
 */
char *trim_string(char *str);

/**
 * @brief Create trimmed copy of string
 *
 * @param str String to trim
 * @return Newly allocated trimmed string (caller must free)
 */
char *trim_string_copy(const char *str);

/**
 * @brief Check if string starts with prefix
 *
 * @param str String to check
 * @param prefix Prefix to look for
 * @return true if str starts with prefix
 */
int str_starts_with(const char *str, const char *prefix);

/**
 * @brief Check if string ends with suffix
 *
 * @param str String to check
 * @param suffix Suffix to look for
 * @return true if str ends with suffix
 */
int str_ends_with(const char *str, const char *suffix);

#endif /* STRING_UTILS_H */
