#define _GNU_SOURCE
#include "lkm.h"
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <asm/prctl.h>
#include <dlfcn.h>
#include <sys/syscall.h>
#include <unistd.h>

typedef int (*RealPThreadCreate)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);

typedef struct {
    void *(*original_routine)(void *);
    void *original_arg;
} PThreadWrapperArgs;

extern int shadow_fd;
static pthread_key_t ss_cleanup_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

static void ss_dtor(void *arg) {
    struct ss_chunk *chunk = arg;
    if (chunk && shadow_fd >= 0) {
        struct ioctl_params req = { .error = 0, .addr = (unsigned long long)chunk };
        ioctl(shadow_fd, IOCTL_SHADOW_FREE, &req);
    }
}

static void make_key(void) {
    pthread_key_create(&ss_cleanup_key, ss_dtor);
}

static inline void get_chunk(struct ss_chunk *chunk) {
     if (syscall(SYS_arch_prctl, ARCH_SET_GS, (unsigned long)chunk) != 0) {
        perror("[ss] arch_prctl failed");
        exit(EXIT_FAILURE);
    }
} 

__attribute__((constructor))
void SSInit(void) {
    struct ss_chunk *main_chunk = MemPoolAlloc();
    if (!main_chunk) {
        exit(EXIT_FAILURE);
    }
    get_chunk(main_chunk);
}

void SSThreadInit(void) {
    pthread_once(&key_once, make_key);

    struct ss_chunk *chunk = MemPoolAlloc();
    if (!chunk) {
        exit(EXIT_FAILURE);
    }

    get_chunk(chunk);
    pthread_setspecific(ss_cleanup_key, chunk);
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
