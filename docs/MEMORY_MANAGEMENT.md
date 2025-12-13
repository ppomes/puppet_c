# Memory Management Documentation

## Overview

The Puppet C parser implements comprehensive memory tracking and leak detection to ensure robust memory management. This system is designed to catch memory leaks during development and testing.

## Components

### 1. Memory Tracking System (`puppet_memory.h/c`)

- **Tracked Allocation**: All allocations use wrapper functions that record file/line information
- **Leak Detection**: Automatic reporting of memory leaks on program exit
- **Thread Safety**: Mutex-protected hash table for concurrent access
- **Statistics**: Comprehensive memory usage tracking

### 2. Memory Wrapper Functions

Replace standard C memory functions:

```c
puppet_malloc(size)     // Instead of malloc(size)
puppet_calloc(n, size)  // Instead of calloc(n, size)  
puppet_free(ptr)        // Instead of free(ptr)
puppet_strdup(str)      // Instead of strdup(str)
puppet_realloc(ptr, size)  // Instead of realloc(ptr, size)
```

### 3. Configuration

Memory tracking is enabled in debug builds:

```c
#ifdef DEBUG
#define PUPPET_MEMORY_TRACKING 1
#endif
```

## Usage Patterns

### Initialization and Cleanup

```c
int main() {
    puppet_memory_init();    // Initialize tracking
    
    // ... program logic ...
    
    int leaks = puppet_memory_shutdown();  // Report and cleanup
    return leaks > 0 ? 1 : 0;
}
```

### AST Memory Management

All AST structures use tracked allocations and provide cleanup functions:

```c
puppet_program_t *program = /* parsed program */;
puppet_program_destroy(program);  // Recursively frees all structures
```

## Testing

### Valgrind Integration

Run memory leak tests:

```bash
cd tests
make check-memory
```

### Manual Testing

```bash
./src/puppetc test_file.pp  # Reports leaks automatically in debug mode
```

## Current Status

- **AST Memory**: All parser-generated structures use tracked allocations
- **Leak Detection**: Active reporting reduced leaks from 21 to 11 in test suite  
- **Valgrind**: Integrated into build system for automated testing
- **Thread Safety**: Mutex-protected allocation tracking

## Known Issues

1. **Lexer/Parser Leaks**: Flex/Bison generated code has some untracked allocations ("still reachable")
2. **Mixed Allocations**: Some system allocations mixed with tracked ones cause "untracked pointer" warnings
3. **Remaining Leaks**: ~11 tracked leaks in complex test cases, primarily from parser structures

## Best Practices

1. **Always Use Wrappers**: Never use malloc/calloc/free directly
2. **Balanced Allocation**: Every `puppet_malloc` needs a `puppet_free`
3. **Test Memory**: Run `make check-memory` before commits
4. **Check Warnings**: Address "untracked pointer" warnings in development

## Memory Statistics

The system tracks:
- Total allocations and frees
- Current active allocations
- Peak memory usage
- Leak detection at shutdown

Statistics are automatically printed in debug mode or can be queried programmatically.