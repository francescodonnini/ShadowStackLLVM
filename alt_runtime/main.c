#define _GNU_SOURCE
#include "lkm.h"
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <asm/prctl.h>
#include <dlfcn.h>
#include <immintrin.h>
#include <sys/syscall.h>
#include <unistd.h>
#define STACK_START 600000001000ULL
#define STACK_SIZE  (1024 * 1024)

pthread_local int shadow_fd = -1;

typedef pid_t (*real_fork_t)(void);
typedef int   (*real_pthread_create_t)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);

static real_fork_t real_fork = NULL;
static real_pthread_create_t real_pthread_create = NULL;

typedef struct {
    void *(*original_routine)(void *);
    void *original_arg;
} pthread_wargs;

static struct ss_chunk *main_thread_chunk = NULL;
static pthread_key_t ss_cleanup_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

static void ss_dtor(void *arg) {
    struct ss_chunk *chunk = arg;
    if (chunk && shadow_fd >= 0) {
        struct ioctl_mem_params req = { .error = 0, .addr = (unsigned long long)chunk };
        ioctl(shadow_fd, IOCTL_SHADOW_FREE, &req);
    }
}

static void make_key(void) {
    pthread_key_create(&ss_cleanup_key, ss_dtor);
}

__attribute__((target("fsgsbase")))
static inline void get_chunk(struct ss_chunk *chunk) {
    // if (syscall(SYS_arch_prctl, ARCH_SET_GS, (unsigned long)chunk) != 0) {
    //     fprintf(stderr, "cannot map %p to gs\n", chunk);
    //     perror("arch_prctl failed");
    //     exit(EXIT_FAILURE);
    // }
    _writegsbase_u64((unsigned long long)chunk);
} 

__attribute__((constructor))
void shadow_stack_init(void) {
    real_fork = (real_fork_t)dlsym(RTLD_NEXT, "fork");
    real_pthread_create = (real_pthread_create_t)dlsym(RTLD_NEXT, "pthread_create");

    shadow_fd = open("/dev/shadowstack", O_RDWR);
    if (shadow_fd < 0) {
        perror("cannot open shadowstack device");
        exit(EXIT_FAILURE);
    }

    main_thread_chunk = mem_pool_alloc();
    if (!main_thread_chunk) {
        exit(EXIT_FAILURE);
    }
    get_chunk(main_thread_chunk);
}

void shadow_stack_thread_init(void) {
    pthread_once(&key_once, make_key);

    struct ss_chunk *chunk = mem_pool_alloc();
    if (!chunk) {
        exit(EXIT_FAILURE);
    }

    get_chunk(chunk);
    pthread_setspecific(ss_cleanup_key, chunk);
}

static void *thread_trampoline(void *arg) {
    pthread_wargs *w_args = (pthread_wargs *)arg;
    
    shadow_stack_thread_init();

    void *(*original_routine)(void *) = w_args->original_routine;
    void *original_data = w_args->original_arg;
    free(w_args);

    return original_routine(original_data);
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg) {
    pthread_wargs *wrapper_args = malloc(sizeof(pthread_wargs));
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

pid_t fork(void) {
    int sy_pipe[2];
    pipe(sy_pipe);
    
    pid_t p_tgid = getpid();
    pid_t p_pid = gettid();

    pid_t pid = real_fork();
    if (!pid) {
        close(sy_pipe[0]);

        struct ioctl_fork_params req = {
            .error = 0,
            .p_pid = p_pid,
            .p_tgid = p_tgid,
        };

        if (shadow_fd >= 0) {
            close(shadow_fd); 
        }
        shadow_fd = open("/dev/shadowstack", O_RDWR);

        ioctl(shadow_fd, IOCTL_SHADOW_FORK, &req);

        char done = '1';
        write(sy_pipe[1], &done, 1);
        close(sy_pipe[1]);
    } else if (pid > 0) {
        close(sy_pipe[1]);

        char done;
        read(sy_pipe[0], &done, 1);
        close(sy_pipe[0]);
    } else {
        close(sy_pipe[0]);
        close(sy_pipe[1]);
    }

    return pid;
}