# Malloc Failure Handling in Puppet C Parser

## Current Situation

**CRITICAL**: The current codebase has inconsistent malloc failure handling that could lead to crashes.

### What Happens When malloc() Fails?

When `malloc()`, `calloc()`, or `realloc()` fail, they return `NULL`. Our tracking functions correctly pass this `NULL` through, but **most calling code assumes allocation always succeeds**.

## Problem Examples

```c
// UNSAFE - Will crash if malloc fails
puppet_value_t *value = puppet_calloc(1, sizeof(puppet_value_t));
value->type = PUPPET_VALUE_UNDEF;  // CRASH if value is NULL
```

```c
// UNSAFE - Parser grammar assumes success
$$ = puppet_calloc(1, sizeof(puppet_stmt_t));
$$->type = PUPPET_STMT_RESOURCE;  // CRASH if $$ is NULL
```

## Current Safety Measures

### 1. Error Reporting (DEBUG mode)
```c
void *puppet_malloc_tracked(size_t size, const char *file, int line) {
    void *ptr = malloc(size);
    if (ptr) {
        add_allocation(ptr, size, file, line);
    } else {
        #ifdef DEBUG
        fprintf(stderr, "WARNING: malloc failed at %s:%d (size=%zu)\n", file, line, size);
        #endif
    }
    return ptr;  // Returns NULL on failure
}
```

### 2. Safe Allocation Macros
```c
// For critical allocations that must succeed
#define PUPPET_MALLOC_OR_DIE(size) ({ \
    void *ptr = puppet_malloc(size); \
    if (!ptr) { \
        fprintf(stderr, "Fatal: Memory allocation failed at %s:%d (size=%zu)\n", \
                __FILE__, __LINE__, (size_t)(size)); \
        exit(1); \
    } \
    ptr; \
})

#define PUPPET_CALLOC_OR_DIE(nmemb, size) ({ \
    void *ptr = puppet_calloc(nmemb, size); \
    if (!ptr) { \
        fprintf(stderr, "Fatal: Memory allocation failed at %s:%d (nmemb=%zu, size=%zu)\n", \
                __FILE__, __LINE__, (size_t)(nmemb), (size_t)(size)); \
        exit(1); \
    } \
    ptr; \
})
```

## Recommended Solutions

### 1. Critical Path Protection
For parser and core AST operations where failure is unrecoverable:
```c
// Instead of:
$$ = puppet_calloc(1, sizeof(puppet_stmt_t));

// Use:
$$ = PUPPET_CALLOC_OR_DIE(1, sizeof(puppet_stmt_t));
```

### 2. Graceful Degradation
For API functions that can return error codes:
```c
puppet_value_t *puppet_value_create_undef(void) {
    puppet_value_t *value = puppet_calloc(1, sizeof(puppet_value_t));
    if (!value) {
        return NULL;  // Caller must check return value
    }
    value->type = PUPPET_VALUE_UNDEF;
    return value;
}

// Caller must check:
puppet_value_t *val = puppet_value_create_undef();
if (!val) {
    // Handle allocation failure
    return ERROR_OUT_OF_MEMORY;
}
```

### 3. Defensive Programming
Always check allocation results in non-critical paths:
```c
char *buffer = puppet_malloc(size);
if (!buffer) {
    // Fallback: use static buffer, return error, or retry with smaller size
    return handle_allocation_failure();
}
```

## When malloc() Fails

### Common Causes
1. **System out of memory** - No more RAM/swap available
2. **Process memory limit** - ulimit or container limits reached  
3. **Memory fragmentation** - Large allocations can't find contiguous blocks
4. **Corrupted heap** - Previous buffer overruns damaged malloc structures

### Detection and Recovery
```c
#include <errno.h>

void *safe_alloc(size_t size) {
    void *ptr = puppet_malloc(size);
    if (!ptr) {
        if (errno == ENOMEM) {
            fprintf(stderr, "Out of memory: requested %zu bytes\n", size);
            // Possible recovery strategies:
            // 1. Free caches/buffers
            // 2. Request smaller allocation
            // 3. Gracefully abort current operation
        }
        return NULL;
    }
    return ptr;
}
```

## Testing malloc Failures

### Manual Testing
```bash
# Limit process memory to 10MB to trigger failures
ulimit -v 10240
./src/puppetc large_file.pp

# Use memory pressure tools
stress-ng --vm 1 --vm-bytes 90% --timeout 60s &
./src/puppetc test.pp
```

### Automated Testing
```c
// Mock malloc to simulate failures
#ifdef TEST_MODE
static int fail_after_n_allocs = -1;
static int alloc_count = 0;

void set_malloc_failure_point(int n) {
    fail_after_n_allocs = n;
    alloc_count = 0;
}

void *test_malloc(size_t size) {
    if (fail_after_n_allocs >= 0 && ++alloc_count > fail_after_n_allocs) {
        errno = ENOMEM;
        return NULL;
    }
    return malloc(size);
}
#endif
```

## Current Status

✅ **Error Detection**: malloc failures are logged in DEBUG mode  
✅ **Safe Macros**: PUPPET_MALLOC_OR_DIE available for critical code  
⚠️  **Inconsistent Handling**: Most code assumes allocation success  
⚠️  **Parser Vulnerability**: Grammar rules don't check for NULL  
⚠️  **API Gaps**: Some functions can't communicate allocation failures  

## Recommended Actions

1. **Immediate**: Use PUPPET_CALLOC_OR_DIE in parser grammar for critical allocations
2. **Short-term**: Add NULL checks to all value creation functions  
3. **Long-term**: Implement comprehensive error propagation through APIs
4. **Testing**: Add malloc failure simulation to test suite

## Philosophy

**For a parser/compiler tool**: Failing fast with clear error messages is often better than attempting to continue with corrupted state. The `*_OR_DIE` macros are appropriate for most parser operations where graceful recovery is not possible.