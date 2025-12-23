/**
 * @file puppet_memory.c
 * @brief Implementation of memory tracking and leak detection
 */

#include "puppet_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>

/*
 * ===========================================================================
 * MEMORY TRACKING STRUCTURES
 * ===========================================================================
 */

#if PUPPET_MEMORY_TRACKING

/**
 * @brief Information about a single memory allocation
 */
typedef struct puppet_alloc_info {
    void *ptr;                    /**< Allocated pointer */
    size_t size;                  /**< Size of allocation */
    const char *file;            /**< Source file of allocation */
    int line;                    /**< Line number of allocation */
    struct puppet_alloc_info *next; /**< Next in hash chain */
} puppet_alloc_info_t;

/**
 * @brief Hash table for tracking allocations
 */
#define ALLOC_HASH_SIZE 1024
static puppet_alloc_info_t *alloc_table[ALLOC_HASH_SIZE];

/**
 * @brief Global memory statistics
 */
static puppet_memory_stats_t memory_stats = {0};

/**
 * @brief Whether memory tracking is enabled
 */
static bool tracking_enabled = true;

/**
 * @brief Whether memory system has been initialized
 */
static bool memory_initialized = false;

/**
 * @brief Mutex for thread safety
 */
static pthread_mutex_t memory_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * ===========================================================================
 * INTERNAL FUNCTIONS
 * ===========================================================================
 */

/**
 * @brief Hash function for pointer addresses
 */
static unsigned int hash_ptr(void *ptr) {
    uintptr_t addr = (uintptr_t)ptr;
    return (unsigned int)((addr >> 3) % ALLOC_HASH_SIZE);
}

/**
 * @brief Add allocation to tracking table
 */
static void add_allocation(void *ptr, size_t size, const char *file, int line) {
    if (!tracking_enabled || !ptr) return;
    
    pthread_mutex_lock(&memory_mutex);
    
    puppet_alloc_info_t *info = (puppet_alloc_info_t*)malloc(sizeof(puppet_alloc_info_t));
    if (!info) {
        pthread_mutex_unlock(&memory_mutex);
        return; /* Can't track this allocation */
    }
    
    info->ptr = ptr;
    info->size = size;
    info->file = file;
    info->line = line;
    
    unsigned int bucket = hash_ptr(ptr);
    info->next = alloc_table[bucket];
    alloc_table[bucket] = info;
    
    /* Update statistics */
    memory_stats.total_allocations++;
    memory_stats.current_allocations++;
    memory_stats.total_bytes_allocated += size;
    memory_stats.current_bytes_used += size;
    
    if (memory_stats.current_allocations > memory_stats.peak_allocations) {
        memory_stats.peak_allocations = memory_stats.current_allocations;
    }
    if (memory_stats.current_bytes_used > memory_stats.peak_bytes_used) {
        memory_stats.peak_bytes_used = memory_stats.current_bytes_used;
    }
    
    pthread_mutex_unlock(&memory_mutex);
}

/**
 * @brief Remove allocation from tracking table
 */
static puppet_alloc_info_t *remove_allocation(void *ptr) {
    if (!tracking_enabled || !ptr) return NULL;
    
    pthread_mutex_lock(&memory_mutex);
    
    unsigned int bucket = hash_ptr(ptr);
    puppet_alloc_info_t **current = &alloc_table[bucket];
    
    while (*current) {
        if ((*current)->ptr == ptr) {
            puppet_alloc_info_t *found = *current;
            *current = found->next;
            
            /* Update statistics */
            memory_stats.total_frees++;
            memory_stats.current_allocations--;
            memory_stats.current_bytes_used -= found->size;
            
            pthread_mutex_unlock(&memory_mutex);
            return found;
        }
        current = &(*current)->next;
    }
    
    pthread_mutex_unlock(&memory_mutex);
    return NULL; /* Not found - possible double free or untracked pointer */
}

/**
 * @brief Find allocation information
 */
static puppet_alloc_info_t *find_allocation(void *ptr) {
    if (!tracking_enabled || !ptr) return NULL;
    
    pthread_mutex_lock(&memory_mutex);
    
    unsigned int bucket = hash_ptr(ptr);
    puppet_alloc_info_t *current = alloc_table[bucket];
    
    while (current) {
        if (current->ptr == ptr) {
            pthread_mutex_unlock(&memory_mutex);
            return current;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&memory_mutex);
    return NULL;
}

/*
 * ===========================================================================
 * PUBLIC API IMPLEMENTATION
 * ===========================================================================
 */

void puppet_memory_init(void) {
    if (memory_initialized) return;
    
    pthread_mutex_lock(&memory_mutex);
    
    /* Clear allocation table */
    for (int i = 0; i < ALLOC_HASH_SIZE; i++) {
        alloc_table[i] = NULL;
    }
    
    /* Clear statistics */
    memset(&memory_stats, 0, sizeof(memory_stats));
    
    memory_initialized = true;
    tracking_enabled = true;
    
    pthread_mutex_unlock(&memory_mutex);
}

int puppet_memory_shutdown(void) {
    if (!memory_initialized) return 0;
    
    pthread_mutex_lock(&memory_mutex);
    
    int leak_count = 0;
    bool found_leaks = false;
    
    /* Check for leaks */
    for (int i = 0; i < ALLOC_HASH_SIZE; i++) {
        puppet_alloc_info_t *current = alloc_table[i];
        while (current) {
            if (!found_leaks) {
                fprintf(stderr, "\nMemory leaks detected:\n");
                fprintf(stderr, "========================\n");
                found_leaks = true;
            }
            
            #if PUPPET_MEMORY_DEBUG_INFO
            fprintf(stderr, "LEAK: %zu bytes at %p (allocated at %s:%d)\n",
                    current->size, current->ptr, 
                    current->file ? current->file : "unknown",
                    current->line);
            #else
            fprintf(stderr, "LEAK: %zu bytes at %p\n", current->size, current->ptr);
            #endif
            
            leak_count++;
            current = current->next;
        }
    }
    
    if (found_leaks) {
        fprintf(stderr, "\nTotal leaks: %d allocations\n", leak_count);
    }
    
    /* Clean up tracking structures */
    for (int i = 0; i < ALLOC_HASH_SIZE; i++) {
        puppet_alloc_info_t *current = alloc_table[i];
        while (current) {
            puppet_alloc_info_t *next = current->next;
            free(current);  /* Use system free for tracking structures */
            current = next;
        }
        alloc_table[i] = NULL;
    }
    
    memory_initialized = false;
    
    pthread_mutex_unlock(&memory_mutex);
    
    return leak_count;
}

void puppet_memory_get_stats(puppet_memory_stats_t *stats) {
    if (!stats) return;
    
    pthread_mutex_lock(&memory_mutex);
    *stats = memory_stats;
    pthread_mutex_unlock(&memory_mutex);
}

void puppet_memory_print_stats(void) {
    puppet_memory_stats_t stats;
    puppet_memory_get_stats(&stats);
    
    printf("\nMemory Statistics:\n");
    printf("==================\n");
    printf("Total allocations: %zu\n", stats.total_allocations);
    printf("Total frees:       %zu\n", stats.total_frees);
    printf("Current allocs:    %zu\n", stats.current_allocations);
    printf("Peak allocs:       %zu\n", stats.peak_allocations);
    printf("Total bytes:       %zu\n", stats.total_bytes_allocated);
    printf("Current bytes:     %zu\n", stats.current_bytes_used);
    printf("Peak bytes:        %zu\n", stats.peak_bytes_used);
    printf("\n");
}

void puppet_memory_print_leaks(void) {
    if (!memory_initialized) return;
    
    pthread_mutex_lock(&memory_mutex);
    
    bool found_any = false;
    
    printf("\nCurrent Allocations:\n");
    printf("====================\n");
    
    for (int i = 0; i < ALLOC_HASH_SIZE; i++) {
        puppet_alloc_info_t *current = alloc_table[i];
        while (current) {
            #if PUPPET_MEMORY_DEBUG_INFO
            printf("ALLOC: %zu bytes at %p (allocated at %s:%d)\n",
                   current->size, current->ptr,
                   current->file ? current->file : "unknown",
                   current->line);
            #else
            printf("ALLOC: %zu bytes at %p\n", current->size, current->ptr);
            #endif
            
            found_any = true;
            current = current->next;
        }
    }
    
    if (!found_any) {
        printf("No active allocations\n");
    }
    
    printf("\n");
    
    pthread_mutex_unlock(&memory_mutex);
}

bool puppet_memory_is_tracked(void *ptr) {
    return find_allocation(ptr) != NULL;
}

void puppet_memory_set_tracking(bool enabled) {
    pthread_mutex_lock(&memory_mutex);
    tracking_enabled = enabled;
    pthread_mutex_unlock(&memory_mutex);
}

/*
 * ===========================================================================
 * TRACKED ALLOCATION FUNCTIONS
 * ===========================================================================
 */

void *puppet_malloc_tracked(size_t size, const char *file, int line) {
    void *ptr = malloc(size);
    if (ptr) {
        add_allocation(ptr, size, file, line);
    } else {
        #ifdef DEBUG
        fprintf(stderr, "WARNING: malloc failed at %s:%d (size=%zu)\n", file, line, size);
        #endif
    }
    return ptr;
}

void *puppet_calloc_tracked(size_t nmemb, size_t size, const char *file, int line) {
    void *ptr = calloc(nmemb, size);
    if (ptr) {
        add_allocation(ptr, nmemb * size, file, line);
    } else {
        #ifdef DEBUG
        fprintf(stderr, "WARNING: calloc failed at %s:%d (nmemb=%zu, size=%zu)\n", file, line, nmemb, size);
        #endif
    }
    return ptr;
}

void *puppet_realloc_tracked(void *ptr, size_t size, const char *file, int line) {
    /* Handle realloc edge cases */
    if (!ptr) {
        /* realloc(NULL, size) == malloc(size) */
        return puppet_malloc_tracked(size, file, line);
    }
    
    if (size == 0) {
        /* realloc(ptr, 0) == free(ptr) */
        puppet_free_tracked(ptr, file, line);
        return NULL;
    }
    
    /* Remove old allocation tracking */
    puppet_alloc_info_t *old_info = remove_allocation(ptr);
    
    void *new_ptr = realloc(ptr, size);
    if (new_ptr) {
        add_allocation(new_ptr, size, file, line);
    } else {
        #ifdef DEBUG
        fprintf(stderr, "WARNING: realloc failed at %s:%d (ptr=%p, size=%zu)\n", file, line, ptr, size);
        #endif
        if (old_info) {
            /* realloc failed, restore old tracking */
            add_allocation(ptr, old_info->size, old_info->file, old_info->line);
        }
    }
    
    if (old_info) {
        free(old_info);
    }
    
    return new_ptr;
}

char *puppet_strdup_tracked(const char *s, const char *file, int line) {
    if (!s) return NULL;
    
    size_t len = strlen(s) + 1;
    char *ptr = (char*)malloc(len);
    if (ptr) {
        memcpy(ptr, s, len);
        add_allocation(ptr, len, file, line);
    } else {
        #ifdef DEBUG
        fprintf(stderr, "WARNING: strdup failed at %s:%d (string='%.*s', len=%zu)\n", 
                file, line, (int)(len > 50 ? 50 : len-1), s, len);
        #endif
    }
    return ptr;
}

void puppet_free_tracked(void *ptr, const char *file, int line) {
    if (!ptr) return;
    
    puppet_alloc_info_t *info = remove_allocation(ptr);
    if (!info) {
        fprintf(stderr, "WARNING: Attempting to free untracked pointer %p at %s:%d\n",
                ptr, file, line);
        /* Still call free() in case it's a valid system allocation */
    } else {
        free(info); /* Free the tracking structure */
    }
    
    free(ptr);
}

#else /* !PUPPET_MEMORY_TRACKING */

/* Stub implementations when tracking is disabled */
void puppet_memory_init(void) {}
int puppet_memory_shutdown(void) { return 0; }
void puppet_memory_get_stats(puppet_memory_stats_t *stats) {
    if (stats) memset(stats, 0, sizeof(*stats));
}
void puppet_memory_print_stats(void) {}
void puppet_memory_print_leaks(void) {}
bool puppet_memory_is_tracked(void *ptr) { (void)ptr; return false; }
void puppet_memory_set_tracking(bool enabled) { (void)enabled; }

#endif /* PUPPET_MEMORY_TRACKING */