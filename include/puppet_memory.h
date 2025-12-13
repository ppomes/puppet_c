/**
 * @file puppet_memory.h
 * @brief Memory management and leak detection for Puppet C parser
 *
 * This module provides memory allocation tracking and debugging capabilities
 * to help identify memory leaks and allocation issues in the parser.
 *
 * Features:
 * - Allocation tracking with file/line information
 * - Memory leak detection and reporting
 * - Double-free detection
 * - Memory usage statistics
 * - Optional Valgrind integration
 */

#ifndef PUPPET_MEMORY_H
#define PUPPET_MEMORY_H

#include <stddef.h>
#include <stdbool.h>

/*
 * ===========================================================================
 * MEMORY TRACKING CONFIGURATION
 * ===========================================================================
 */

/* Enable memory tracking in debug builds */
#ifdef DEBUG
#define PUPPET_MEMORY_TRACKING 1
#else
#define PUPPET_MEMORY_TRACKING 0
#endif

/* Enable detailed allocation info (file/line) */
#define PUPPET_MEMORY_DEBUG_INFO 1

/*
 * ===========================================================================
 * MEMORY ALLOCATION WRAPPERS
 * ===========================================================================
 */

#if PUPPET_MEMORY_TRACKING

/* Tracked allocation functions */
void *puppet_malloc_tracked(size_t size, const char *file, int line);
void *puppet_calloc_tracked(size_t nmemb, size_t size, const char *file, int line);
void *puppet_realloc_tracked(void *ptr, size_t size, const char *file, int line);
char *puppet_strdup_tracked(const char *s, const char *file, int line);
void puppet_free_tracked(void *ptr, const char *file, int line);

/* Convenience macros that automatically pass file/line info */
#define puppet_malloc(size) puppet_malloc_tracked(size, __FILE__, __LINE__)
#define puppet_calloc(nmemb, size) puppet_calloc_tracked(nmemb, size, __FILE__, __LINE__)
#define puppet_realloc(ptr, size) puppet_realloc_tracked(ptr, size, __FILE__, __LINE__)
#define puppet_strdup(s) puppet_strdup_tracked(s, __FILE__, __LINE__)
#define puppet_free(ptr) puppet_free_tracked(ptr, __FILE__, __LINE__)

#else

/* Direct system calls when tracking is disabled */
#include <stdlib.h>
#include <string.h>
#define puppet_malloc(size) malloc(size)
#define puppet_calloc(nmemb, size) calloc(nmemb, size)
#define puppet_realloc(ptr, size) realloc(ptr, size)
#define puppet_strdup(s) strdup(s)
#define puppet_free(ptr) free(ptr)

#endif /* PUPPET_MEMORY_TRACKING */

/*
 * ===========================================================================
 * MEMORY TRACKING API
 * ===========================================================================
 */

/**
 * @brief Memory allocation statistics
 */
typedef struct puppet_memory_stats {
    size_t total_allocations;      /**< Total number of allocations */
    size_t total_frees;           /**< Total number of frees */
    size_t current_allocations;    /**< Currently active allocations */
    size_t peak_allocations;      /**< Peak number of active allocations */
    size_t total_bytes_allocated; /**< Total bytes ever allocated */
    size_t current_bytes_used;    /**< Current bytes in use */
    size_t peak_bytes_used;       /**< Peak memory usage */
} puppet_memory_stats_t;

/**
 * @brief Initialize memory tracking system
 * 
 * Should be called once at program startup before any allocations.
 */
void puppet_memory_init(void);

/**
 * @brief Shutdown memory tracking and report leaks
 * 
 * Should be called at program exit. Reports any memory leaks
 * and cleans up tracking structures.
 * 
 * @return Number of leaked allocations
 */
int puppet_memory_shutdown(void);

/**
 * @brief Get current memory usage statistics
 * @param stats Pointer to stats structure to fill
 */
void puppet_memory_get_stats(puppet_memory_stats_t *stats);

/**
 * @brief Print current memory usage statistics
 */
void puppet_memory_print_stats(void);

/**
 * @brief Print all currently allocated memory blocks
 * 
 * Useful for debugging memory leaks. Shows file/line information
 * if PUPPET_MEMORY_DEBUG_INFO is enabled.
 */
void puppet_memory_print_leaks(void);

/**
 * @brief Check if a pointer was allocated by our tracking system
 * @param ptr Pointer to check
 * @return true if tracked, false otherwise
 */
bool puppet_memory_is_tracked(void *ptr);

/**
 * @brief Enable/disable memory tracking at runtime
 * @param enabled Whether to enable tracking
 */
void puppet_memory_set_tracking(bool enabled);

/*
 * ===========================================================================
 * CONVENIENCE MACROS FOR COMMON PATTERNS
 * ===========================================================================
 */

/**
 * @brief Safe allocation with error checking
 * 
 * Allocates memory and exits program on failure.
 * Should only be used where allocation failure is fatal.
 */
#define PUPPET_MALLOC_OR_DIE(size) ({ \
    void *ptr = puppet_malloc(size); \
    if (!ptr) { \
        fprintf(stderr, "Fatal: Memory allocation failed at %s:%d (size=%zu)\n", __FILE__, __LINE__, (size_t)(size)); \
        exit(1); \
    } \
    ptr; \
})

/**
 * @brief Safe calloc with error checking
 */
#define PUPPET_CALLOC_OR_DIE(nmemb, size) ({ \
    void *ptr = puppet_calloc(nmemb, size); \
    if (!ptr) { \
        fprintf(stderr, "Fatal: Memory allocation failed at %s:%d (nmemb=%zu, size=%zu)\n", __FILE__, __LINE__, (size_t)(nmemb), (size_t)(size)); \
        exit(1); \
    } \
    ptr; \
})

/**
 * @brief Safe free that nullifies pointer
 */
#define PUPPET_FREE(ptr) do { \
    if (ptr) { \
        puppet_free(ptr); \
        (ptr) = NULL; \
    } \
} while(0)

/**
 * @brief Allocate and zero-initialize structure
 */
#define PUPPET_NEW(type) ((type*)puppet_calloc(1, sizeof(type)))

/**
 * @brief Allocate array of structures
 */
#define PUPPET_NEW_ARRAY(type, count) ((type*)puppet_calloc(count, sizeof(type)))

#endif /* PUPPET_MEMORY_H */