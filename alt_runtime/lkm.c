#include "lkm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

extern int shadow_fd;

struct ss_chunk* mem_pool_alloc(void) {
    struct ioctl_mem_params req = { .error = 0, .addr = 0 };
    long err = ioctl(shadow_fd, IOCTL_SHADOW_REQ, &req);
    if (err < 0 || req.error) {
        perror("ioctl");
        return NULL;
    }
    
    struct ss_chunk *chunk = (struct ss_chunk *)req.addr;

    printf("got address %p\n", chunk);

    chunk->padding = NULL;
    chunk->top = chunk->data;
    return chunk;
}