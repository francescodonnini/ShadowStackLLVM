#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// 1. Test a global store
uint64_t global_val = 0;

void test_normal_stores() {
    // 2. Test a stack store
    volatile uint64_t local_val = 10;
    local_val = 20;

    // 3. Test a heap store
    volatile uint64_t *heap_ptr = malloc(sizeof(uint64_t));
    if (heap_ptr) {
        *heap_ptr = 30;
        free((void*)heap_ptr);
    }
    
    global_val = 40;
}

void test_sandbox_violation() {
    // 4. Test an out-of-bounds pointer (Above 0x700000000000)
    // We use 0x7FFFFFFFFFFF to make it painfully obvious if the mask works.
    uint64_t bad_addr = 0x7FFFFFFFFFFF;
    volatile uint64_t *bad_ptr = (uint64_t *)bad_addr;

    printf("[*] Original target address : %p\n", (void*)bad_ptr);
    printf("[*] Attempting store...\n");

    // If your pass is NOT working, this will segfault at 0x7FFFFFFFFFFF.
    // If your pass IS working, this will segfault at the masked address 
    // (e.g., 0x0FFFFFFFFFFF), OR it will succeed if you mapped that masked address!
    *bad_ptr = 42; 

    printf("[+] Store executed without crashing!\n");
}

int main() {
    printf("--- Testing Normal Stores ---\n");
    test_normal_stores();
    
    printf("\n--- Testing Sandboxed Store ---\n");
    test_sandbox_violation();
    
    return 0;
}