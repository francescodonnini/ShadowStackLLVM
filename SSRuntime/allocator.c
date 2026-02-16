#include "allocator.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/mman.h>
#define MAX_TRIES (100)

static void *mem_pool;

static atomic_flag llock = ATOMIC_FLAG_INIT;

static SSChunk *freelist;

static uint64_t chunk_size;

static _Atomic(uint8_t *) freearea_start;

static void *freearea_end;

static inline void lock_list(void) {
    while (atomic_flag_test_and_set(&llock)) {
        sched_yield();
    }
}

static inline void unlock_list(void) {
    atomic_flag_clear(&llock);
}

int MemPoolInit(void *adr, uint64_t ch_size, uint64_t pl_size) {
    mem_pool = mmap(adr, pl_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (mem_pool == MAP_FAILED) {
        perror("[ss-alloc] mmap");
        return -1;
    }

    freelist = NULL;
    chunk_size = ch_size;
    freearea_start = mem_pool;
    freearea_end = mem_pool + pl_size;
    return 0;
}

static inline void init_chunk(SSChunk *chunk) {
    chunk->top = chunk->data;
    chunk->next = NULL;
}

static SSChunk* alloc_chunk(void *lo) {
    if (lo + chunk_size >= freearea_end) {
        fprintf(stderr, "[ss-alloc] out of memory\n");
        return NULL;
    }

    void *p = mmap(lo, chunk_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p == MAP_FAILED) {
        perror("[ss-alloc] mmap failed");
        return NULL;
    }
    init_chunk((SSChunk*)p);
    return (SSChunk*)p;
}

static SSChunk* recycle_chunk(void) {
    if (freelist) {
        lock_list();
        if (freelist) {
            SSChunk *p = freelist;
            freelist = p->next;
            unlock_list();
            return p;
        }
        unlock_list();
    }
    return NULL;
}

SSChunk* MemPoolAlloc(void) {
    SSChunk *p = recycle_chunk();
    if (p) {
        return p;
    }
    void *lo = atomic_fetch_add(&freearea_start, chunk_size);
    return alloc_chunk(lo);
}

void MemPoolRelease(SSChunk *chunk) {
    lock_list();
    chunk->next = freelist;
    freelist = chunk;
    unlock_list();
}
