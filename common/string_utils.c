/**
 * @file string_utils.c
 * @brief String manipulation utilities implementation
 */

#include "string_utils.h"
#include "puppet_memory.h"
#include <string.h>
#include <ctype.h>

char *safe_strdup(const char *s) {
    return s ? puppet_strdup(s) : NULL;
}

char *trim_string(char *str) {
    if (!str) return NULL;

    /* Trim leading whitespace */
    while (isspace((unsigned char)*str)) str++;

    if (*str == '\0') return str;

    /* Trim trailing whitespace */
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';

    return str;
}

char *trim_string_copy(const char *str) {
    if (!str) return NULL;

    /* Skip leading whitespace */
    const char *start = str;
    while (isspace((unsigned char)*start)) start++;

    if (*start == '\0') {
        return puppet_strdup("");
    }

    /* Find end of non-whitespace */
    const char *end = str + strlen(str) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;

    /* Allocate and copy trimmed portion */
    size_t len = end - start + 1;
    char *result = puppet_malloc(len + 1);
    if (!result) return NULL;

    memcpy(result, start, len);
    result[len] = '\0';

    return result;
}

int str_starts_with(const char *str, const char *prefix) {
    if (!str || !prefix) return 0;
    size_t prefix_len = strlen(prefix);
    if (strlen(str) < prefix_len) return 0;
    return memcmp(str, prefix, prefix_len) == 0;
}

int str_ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) return 0;
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (str_len < suffix_len) return 0;
    return memcmp(str + str_len - suffix_len, suffix, suffix_len) == 0;
}
