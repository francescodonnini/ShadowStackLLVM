#define _GNU_SOURCE
#include "allocator.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <asm/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#define SS_SIZE  (4096)
#define SS_BASE  ((void*)0x700000000000)
#define MEM_SIZE (64ULL * 1024 * 1024 * 1024)

static inline void get_chunk(SSChunk *chunk) {
     if (syscall(SYS_arch_prctl, ARCH_SET_GS, (unsigned long)chunk) != 0) {
        perror("[ss] arch_prctl failed");
        exit(EXIT_FAILURE);
    }
} 

__attribute__((constructor))
void SSInit(void) {
    if (MemPoolInit(SS_BASE, SS_SIZE, MEM_SIZE) < 0) {
        exit(EXIT_FAILURE);
    }

    SSChunk *main_chunk = MemPoolAlloc();
    if (!main_chunk) {
        exit(EXIT_FAILURE);
    }
    get_chunk(main_chunk);
}

void SSThreadInit(void) {
    SSChunk *chunk = MemPoolAlloc();
    if (!chunk) {
        exit(EXIT_FAILURE);
    }

    get_chunk(chunk);
}

static inline void put_chunk(SSChunk *chunk) {
    MemPoolRelease(chunk);
}