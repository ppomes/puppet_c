/**
 * Test program to demonstrate malloc failure handling
 * Compile with: gcc -DDEBUG=1 -I../include -o test_malloc_failure test_malloc_failure.c ../src/puppet_memory.c -lpthread
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include "puppet_memory.h"

// Simulate malloc failure by limiting available memory
void limit_memory(size_t limit_mb) {
    struct rlimit rl;
    rl.rlim_cur = limit_mb * 1024 * 1024;  // Convert MB to bytes
    rl.rlim_max = limit_mb * 1024 * 1024;
    
    if (setrlimit(RLIMIT_AS, &rl) == 0) {
        printf("Memory limit set to %zu MB\n", limit_mb);
    } else {
        perror("Failed to set memory limit");
    }
}

int main() {
    puppet_memory_init();
    
    printf("=== Malloc Failure Handling Test ===\n\n");
    
    // Test 1: Normal allocation
    printf("1. Testing normal allocation...\n");
    void *ptr1 = puppet_malloc(1024);
    if (ptr1) {
        printf("   ✓ Normal allocation succeeded\n");
        puppet_free(ptr1);
    } else {
        printf("   ✗ Normal allocation failed unexpectedly\n");
    }
    
    // Test 2: Test with memory limit (this may not trigger on modern systems)
    printf("\n2. Testing with memory pressure...\n");
    limit_memory(10);  // 10MB limit
    
    // Try to allocate more than the limit
    printf("   Attempting to allocate 20MB...\n");
    void *big_ptr = puppet_malloc(20 * 1024 * 1024);
    if (big_ptr) {
        printf("   ✗ Large allocation succeeded unexpectedly\n");
        puppet_free(big_ptr);
    } else {
        printf("   ✓ Large allocation failed as expected\n");
        printf("   (This demonstrates malloc failure detection)\n");
    }
    
    // Test 3: Demonstrate safe allocation macros
    printf("\n3. Testing safe allocation macros...\n");
    printf("   Using PUPPET_MALLOC_OR_DIE for small allocation...\n");
    
    // This should succeed
    void *safe_ptr = PUPPET_MALLOC_OR_DIE(100);
    printf("   ✓ Safe allocation succeeded\n");
    puppet_free(safe_ptr);
    
    // Test 4: Show how to handle allocation failure gracefully
    printf("\n4. Testing graceful failure handling...\n");
    void *test_ptr = puppet_malloc(1000);
    if (test_ptr) {
        printf("   ✓ Allocation succeeded, proceeding normally\n");
        strcpy((char*)test_ptr, "Hello, World!");
        printf("   Data written: %s\n", (char*)test_ptr);
        puppet_free(test_ptr);
    } else {
        printf("   ⚠  Allocation failed, using fallback strategy\n");
        printf("   (In real code: use static buffer, return error, etc.)\n");
    }
    
    printf("\n5. Memory tracking statistics:\n");
    puppet_memory_print_stats();
    
    int leaks = puppet_memory_shutdown();
    printf("\nTest completed with %d memory leaks\n", leaks);
    
    printf("\n=== Key Points ===\n");
    printf("• malloc() can fail and return NULL\n");
    printf("• Our tracking system detects and logs failures (in DEBUG mode)\n");
    printf("• Use PUPPET_MALLOC_OR_DIE() for critical allocations\n");
    printf("• Always check return values for graceful failure handling\n");
    printf("• Memory pressure testing helps find allocation failure bugs\n");
    
    return 0;
}