/**
 * @file color_output.c
 * @brief Colorized terminal output implementation
 */

#include "color_output.h"
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

static bool colors_enabled = false;
static bool initialized = false;

void color_init(void) {
    if (!initialized) {
        colors_enabled = isatty(STDOUT_FILENO);
        initialized = true;
    }
}

void color_set_enabled(bool enabled) {
    colors_enabled = enabled;
    initialized = true;
}

bool color_is_enabled(void) {
    if (!initialized) {
        color_init();
    }
    return colors_enabled;
}

static void print_prefix(const char *color, const char *prefix) {
    if (!initialized) color_init();

    if (colors_enabled) {
        printf("%s%s:%s ", color, prefix, ANSI_RESET);
    } else {
        printf("%s: ", prefix);
    }
}

void print_info(const char *fmt, ...) {
    print_prefix(ANSI_CYAN, "Info");

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

void print_notice(const char *fmt, ...) {
    print_prefix(ANSI_BOLD_GREEN, "Notice");

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

void print_warning(const char *fmt, ...) {
    print_prefix(ANSI_BOLD_YELLOW, "Warning");

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

void print_error(const char *fmt, ...) {
    if (!initialized) color_init();

    if (colors_enabled) {
        fprintf(stderr, "%sError:%s ", ANSI_BOLD_RED, ANSI_RESET);
    } else {
        fprintf(stderr, "Error: ");
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

void print_debug(const char *fmt, ...) {
    print_prefix(ANSI_MAGENTA, "Debug");

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

void print_resource_change(const char *type, const char *title,
                          const char *attribute, const char *fmt, ...) {
    if (!initialized) color_init();

    if (colors_enabled) {
        printf("%sNotice:%s %s/%s[%s]/%s: ",
               ANSI_BOLD_GREEN, ANSI_RESET,
               ANSI_BOLD, type, title, attribute);
    } else {
        printf("Notice: %s[%s]/%s: ", type, title, attribute);
    }

    if (colors_enabled) {
        printf("%s", ANSI_RESET);
    }

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

void print_resource_noop(const char *type, const char *title,
                        const char *attribute, const char *message) {
    if (!initialized) color_init();

    if (colors_enabled) {
        printf("%sNotice:%s %s[%s]/%s: %s %s(noop)%s\n",
               ANSI_BOLD_YELLOW, ANSI_RESET,
               type, title, attribute, message,
               ANSI_YELLOW, ANSI_RESET);
    } else {
        printf("Notice: %s[%s]/%s: %s (noop)\n",
               type, title, attribute, message);
    }
}

void print_resource_error(const char *type, const char *title,
                         const char *fmt, ...) {
    if (!initialized) color_init();

    if (colors_enabled) {
        fprintf(stderr, "%sError:%s %s[%s]: ",
                ANSI_BOLD_RED, ANSI_RESET, type, title);
    } else {
        fprintf(stderr, "Error: %s[%s]: ", type, title);
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

void print_bold(const char *fmt, ...) {
    if (!initialized) color_init();

    if (colors_enabled) {
        printf("%s", ANSI_BOLD);
    }

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    if (colors_enabled) {
        printf("%s", ANSI_RESET);
    }
}

void print_color(const char *color, const char *fmt, ...) {
    if (!initialized) color_init();

    if (colors_enabled) {
        printf("%s", color);
    }

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    if (colors_enabled) {
        printf("%s", ANSI_RESET);
    }
}
