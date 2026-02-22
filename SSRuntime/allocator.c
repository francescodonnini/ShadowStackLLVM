#include "allocator.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <immintrin.h>
#include <sched.h>
#include <sys/mman.h>

static uint8_t            *mem_pool;

static pthread_mutex_t     llock;

static SSChunk            *freelist;

static uint64_t            chunk_size;

static _Atomic(uint8_t *)  freearea_start;

static uint8_t *freearea_end;

int MemPoolInit(uint8_t *adr, uint64_t ch_size, uint64_t pl_size) {
    pthread_mutex_init(&llock, NULL);
    mem_pool = mmap(adr, pl_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
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

static SSChunk* alloc_chunk(uint8_t *lo) {
    if (lo + chunk_size >= freearea_end) {
        fprintf(stderr, "[ss-alloc] out of memory\n");
        return NULL;
    }

    init_chunk((SSChunk*)lo);
    return (SSChunk*)lo;
}

static SSChunk* recycle_chunk(void) {
    if (freelist) {
        pthread_mutex_lock(&llock);
        if (freelist) {
            SSChunk *p = freelist;
            freelist = p->next;
            init_chunk(p);
            pthread_mutex_unlock(&llock);
            return p;
        }
        pthread_mutex_unlock(&llock);
    }
    return NULL;
}

SSChunk* MemPoolAlloc(void) {
    SSChunk *p = recycle_chunk();
    if (p) {
        return p;
    }
    uint8_t *lo = atomic_fetch_add(&freearea_start, chunk_size);
    return alloc_chunk(lo);
}

void MemPoolRelease(SSChunk *chunk) {
    pthread_mutex_lock(&llock);
    chunk->next = freelist;
    freelist = chunk;
    pthread_mutex_unlock(&llock);
}
