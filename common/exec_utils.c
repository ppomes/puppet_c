/**
 * @file exec_utils.c
 * @brief Command execution utilities implementation
 */

#include "exec_utils.h"
#include "puppet_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

int run_command(const char *cmd, char *output, size_t output_size) {
    if (!cmd) return -1;

    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    if (output && output_size > 0) {
        output[0] = '\0';
        size_t total = 0;
        char buf[256];

        while (fgets(buf, sizeof(buf), fp) && total < output_size - 1) {
            size_t len = strlen(buf);
            size_t remaining = output_size - 1 - total;
            if (len > remaining) {
                len = remaining;
            }
            memcpy(output + total, buf, len);
            total += len;
            output[total] = '\0';
        }
    } else {
        /* Drain output even if not capturing */
        char buf[256];
        while (fgets(buf, sizeof(buf), fp)) {
            /* discard */
        }
    }

    int status = pclose(fp);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int exec_command(const char *cmd, bool verbose) {
    if (!cmd) return -1;

    if (verbose) {
        fprintf(stderr, "[EXEC] %s\n", cmd);
    }
    return system(cmd);
}

char *run_command_output(const char *cmd) {
    if (!cmd) return NULL;

    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;

    size_t capacity = 1024;
    size_t size = 0;
    char *output = puppet_malloc(capacity);
    if (!output) {
        pclose(fp);
        return NULL;
    }

    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
        size_t len = strlen(buf);
        if (size + len + 1 > capacity) {
            capacity *= 2;
            char *new_output = puppet_realloc(output, capacity);
            if (!new_output) {
                puppet_free(output);
                pclose(fp);
                return NULL;
            }
            output = new_output;
        }
        memcpy(output + size, buf, len);
        size += len;
    }
    output[size] = '\0';

    pclose(fp);
    return output;
}
