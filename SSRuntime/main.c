#define _GNU_SOURCE
#include "allocator.h"
#include "mapping.h"
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <asm/prctl.h>
#include <dlfcn.h>
#include <sys/syscall.h>
#include <unistd.h>

#define SS_SIZE  (1024 * 1024)
#define SS_BASE  ((uint8_t*)0x7fffffffffff)
#define MEM_SIZE ((1024 * 1024 * 1024))

typedef int (*RealPThreadCreate)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);

typedef struct {
    void *(*original_routine)(void *);
    void *original_arg;
} PThreadWrapperArgs;

static inline void get_chunk(SSChunk *chunk) {
     if (syscall(SYS_arch_prctl, ARCH_SET_GS, (unsigned long)chunk) != 0) {
        perror("[ss] arch_prctl failed");
        exit(EXIT_FAILURE);
    }
} 

__attribute__((constructor))
void SSInit(void) {
    uint64_t lo, hi;
    if (setup_memory(&lo, &hi, MEM_SIZE)) {
        exit(EXIT_FAILURE);
    }

    if (MemPoolInit((uint8_t*)lo, SS_SIZE, MEM_SIZE) < 0) {
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

static void *thread_trampoline(void *arg) {
    PThreadWrapperArgs *w_args = (PThreadWrapperArgs *)arg;
    
    SSThreadInit();

    void *(*original_routine)(void *) = w_args->original_routine;
    void *original_data = w_args->original_arg;
    free(w_args);

    return original_routine(original_data);
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg) {
    static RealPThreadCreate real_pthread_create = NULL;
    if (!real_pthread_create) {
        real_pthread_create = (RealPThreadCreate)dlsym(RTLD_NEXT, "pthread_create");
    }

    PThreadWrapperArgs *wrapper_args = malloc(sizeof(PThreadWrapperArgs));
    if (!wrapper_args) {
        return -1; 
    }

    wrapper_args->original_routine = start_routine;
    wrapper_args->original_arg = arg;
    int ret = real_pthread_create(thread, attr, thread_trampoline, wrapper_args);
    if (ret != 0) {
        free(wrapper_args);
    }
    return ret;
}
