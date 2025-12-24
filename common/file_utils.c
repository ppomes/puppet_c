/**
 * @file file_utils.c
 * @brief File operation utilities implementation
 */

#include "file_utils.h"
#include "puppet_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define DEFAULT_MAX_FILE_SIZE (10 * 1024 * 1024)  /* 10MB */

bool file_exists(const char *path) {
    if (!path) return false;
    struct stat st;
    return stat(path, &st) == 0;
}

bool is_directory(const char *path) {
    if (!path) return false;
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

bool is_symlink(const char *path) {
    if (!path) return false;
    struct stat st;
    if (lstat(path, &st) != 0) return false;
    return S_ISLNK(st.st_mode);
}

bool is_regular_file(const char *path) {
    if (!path) return false;
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISREG(st.st_mode);
}

char *read_file_contents(const char *path, size_t max_size) {
    if (!path) return NULL;

    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (max_size == 0) {
        max_size = DEFAULT_MAX_FILE_SIZE;
    }

    if (size < 0 || (size_t)size > max_size) {
        fclose(f);
        return NULL;
    }

    char *content = puppet_malloc(size + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }

    size_t bytes_read = fread(content, 1, size, f);
    content[bytes_read] = '\0';
    fclose(f);

    return content;
}

char *read_file_line(const char *path) {
    if (!path) return NULL;

    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    char *line = NULL;
    size_t len = 0;
    ssize_t nread = getline(&line, &len, f);
    fclose(f);

    if (nread <= 0) {
        free(line);  /* getline uses malloc, not puppet_malloc */
        return NULL;
    }

    /* Remove trailing newline */
    if (nread > 0 && line[nread - 1] == '\n') {
        line[nread - 1] = '\0';
    }

    /* Copy to puppet_malloc'd memory for consistency */
    char *result = puppet_strdup(line);
    free(line);

    return result;
}

int write_file_contents(const char *path, const char *content) {
    if (!path) return -1;

    FILE *f = fopen(path, "w");
    if (!f) return -1;

    if (content) {
        size_t len = strlen(content);
        if (fwrite(content, 1, len, f) != len) {
            fclose(f);
            return -1;
        }
    }

    fclose(f);
    return 0;
}

long get_file_size(const char *path) {
    if (!path) return -1;

    struct stat st;
    if (stat(path, &st) != 0) return -1;

    return st.st_size;
}
