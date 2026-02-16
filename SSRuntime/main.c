#define _GNU_SOURCE
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <asm/prctl.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#define SS_SIZE  (4096)
#define SS_BASE  ((void*)0x700000000000)
#define MEM_SIZE (64ULL * 1024 * 1024 * 1024)

typedef struct SSChunk {
    uint64_t *top;
    struct SSChunk  *next;
    uint64_t  data[];
} SSChunk;

static SSChunk *freelist = NULL;

static atomic_flag llock = ATOMIC_FLAG_INIT;

static uint8_t *bump_ptr;
static uint8_t *reserved_end;

static inline void lock_list(void) {
    while (atomic_flag_test_and_set(&llock)) {
        sched_yield();
    }
}

static inline void unlock_list(void) {
    atomic_flag_clear(&llock);
}

static SSChunk* alloc_chunk(void) {
    lock_list();
    
    if (bump_ptr + SS_SIZE > reserved_end) {
        unlock_list();
        fprintf(stderr, "[ShadowStack] Fatal: Exhausted 64GB reserved region.\n");
        return NULL;
    }

    void *target = bump_ptr;
    bump_ptr += SS_SIZE;

    unlock_list();

    void *p = mmap(target, SS_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p == MAP_FAILED) {
        perror("[ShadowStack] mmap failed");
        exit(1);
    }
    SSChunk *chunk = (SSChunk*)p;
    chunk->top = chunk->data;

    return chunk;
}

static SSChunk* get_chunk(void) {
    SSChunk *p = NULL;

    lock_list();
    if (freelist != NULL) {
        p = freelist;
        freelist = p->next;
    }
    unlock_list();

    if (p == NULL) {
        p = alloc_chunk();
    } else {
        p->top = p->data;
    }

    return p;
}

static void put_chunk(SSChunk *chunk) {
    if (chunk == NULL) {
        return;
    }

    madvise(chunk, SS_SIZE, MADV_DONTNEED);

    lock_list();
    chunk->next = freelist;
    freelist = chunk;
    unlock_list();
}

__attribute__((constructor))
void SSInit(void) {
    void *pool = mmap(SS_BASE, MEM_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (pool == MAP_FAILED) {
        perror("[ShadowStack] mmap failed");
        exit(1);
    }
    
    if (pool != SS_BASE) {
        fprintf(stderr, "[ShadowStack] Kernel refused address %p. Got %p\n", SS_BASE, pool);
        exit(1);
    }

    bump_ptr = (uint8_t*)pool;
    reserved_end = (uint8_t*)pool + MEM_SIZE;

    SSChunk *main_chunk = get_chunk();
    if (syscall(SYS_arch_prctl, ARCH_SET_GS, (unsigned long)main_chunk) != 0) {
        perror("[ShadowStack] arch_prctl failed");
        exit(1);
    }
}

void SSThreadInit(void) {
    SSChunk *chunk = get_chunk();
    if (!chunk) {
        exit(1);
    }

    if (syscall(SYS_arch_prctl, ARCH_SET_GS, (unsigned long)chunk) != 0) {
        perror("[ShadowStack] arch_prctl failed");
        exit(1);
    }
}

void SSThreadDestroy(void) {
    uint64_t gs;
    if (syscall(SYS_arch_prctl, ARCH_GET_GS, &gs) != 0) {
        perror("[ShadowStack] arch_prctl(ARCH_GET_GS) failed");
        exit(1);
    }
    
    if (!gs) {
        return;
    }

    if (gs < SS_BASE || gs >= reserved_end) {
        fprintf(stderr, "[ShadowStack] gs pointer out of bounds\n");
        exit(1);
    }

    syscall(SYS_arch_prctl, ARCH_SET_GS, 0);
    put_chunk((SSChunk*)gs);
}