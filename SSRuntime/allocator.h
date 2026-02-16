#ifndef ALLOCATOR_H
#define ALLOCATOR_H
#define _GNU_SOURCE
#include <stdint.h>

typedef struct SSChunk {
    uint64_t *top;
    SSChunk  *next;
    uint64_t  data[];
} SSChunk;

int MemPoolInit(void *adr, uint64_t ch_size, uint64_t pl_size);

SSChunk* MemPoolAlloc(void);

void MemPoolRelease(SSChunk *chunk);

#endif