/**
 * @file file_utils.h
 * @brief File operation utilities
 *
 * Provides common functions for file existence checks,
 * type detection, and content reading.
 */

#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Check if a file or directory exists
 * @param path Path to check
 * @return true if exists, false otherwise
 */
bool file_exists(const char *path);

/**
 * @brief Check if path is a directory
 * @param path Path to check
 * @return true if directory, false otherwise
 */
bool is_directory(const char *path);

/**
 * @brief Check if path is a symbolic link
 * @param path Path to check
 * @return true if symlink, false otherwise
 */
bool is_symlink(const char *path);

/**
 * @brief Check if path is a regular file
 * @param path Path to check
 * @return true if regular file, false otherwise
 */
bool is_regular_file(const char *path);

/**
 * @brief Read entire file contents into allocated buffer
 *
 * @param path Path to file
 * @param max_size Maximum size to read (0 for default 10MB limit)
 * @return Newly allocated string with contents (caller must free), or NULL on error
 */
char *read_file_contents(const char *path, size_t max_size);

/**
 * @brief Read first line of file
 *
 * @param path Path to file
 * @return Newly allocated string with first line (no newline), or NULL on error
 */
char *read_file_line(const char *path);

/**
 * @brief Write content to file
 *
 * @param path Path to file
 * @param content Content to write (can be NULL for empty file)
 * @return 0 on success, -1 on error
 */
int write_file_contents(const char *path, const char *content);

/**
 * @brief Get file size
 *
 * @param path Path to file
 * @return File size in bytes, or -1 on error
 */
long get_file_size(const char *path);

#endif /* FILE_UTILS_H */
