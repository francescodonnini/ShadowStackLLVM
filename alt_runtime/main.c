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

typedef pid_t (*real_fork)(void);
typedef int (*real_pthread_create)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);

typedef struct {
    void *(*original_routine)(void *);
    void *original_arg;
} pthread_wargs;

extern int shadow_fd;
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

static inline void get_chunk(struct ss_chunk *chunk) {
     if (syscall(SYS_arch_prctl, ARCH_SET_GS, (unsigned long)chunk) != 0) {
        perror("[ss] arch_prctl failed");
        exit(EXIT_FAILURE);
    }
} 

__attribute__((constructor))
void shadow_stack_init(void) {
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
    static real_pthread_create pthrad_create_cb = NULL;
    if (!pthrad_create_cb) {
        pthrad_create_cb = (real_pthread_create)dlsym(RTLD_NEXT, "pthread_create");
    }

    pthread_wargs *wrapper_args = malloc(sizeof(pthread_wargs));
    if (!wrapper_args) {
        return -1; 
    }

    wrapper_args->original_routine = start_routine;
    wrapper_args->original_arg = arg;
    int ret = pthrad_create_cb(thread, attr, thread_trampoline, wrapper_args);
    if (ret != 0) {
        free(wrapper_args);
    }
    return ret;
}

pid_t fork(void) {
    real_fork fork_cb = (real_fork)dlsym(RTLD_NEXT, "fork");
    
    int sy_pipe[2];
    pipe(sy_pipe);
    
    pid_t p_tgid = getpid();
    pid_t p_pid = gettid();

    pid_t pid = fork_cb();
    if (!pid) {
        close(sy_pipe[0]);

        struct ioctl_fork_params req = {
            .error = 0,
            .p_pid = p_pid,
            .p_tgid = p_tgid,
        };

        int fd = open("/dev/shadowstack", O_RDWR);
        ioctl(fd, IOCTL_SHADOW_FORK, &req);
        close(fd);

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