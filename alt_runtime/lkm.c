#include "lkm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int shadow_fd = -1;

struct ss_chunk* mem_pool_alloc(void) {
    if (shadow_fd < 0) {
        shadow_fd = open("/dev/shadowstack", O_RDWR);
        if (shadow_fd < 0) {
            perror("open");
            return NULL;
        }
    }

    struct ioctl_mem_params req = { .error = 0, .addr = 0 };
    long err = ioctl(shadow_fd, IOCTL_SHADOW_REQ, &req);
    if (err < 0 || !req.error) {
        perror("ioctl");
        return NULL;
    }
    
    struct ss_chunk *chunk = (struct ss_chunk *)req.addr;
    chunk->padding = NULL;
    chunk->top = chunk->data;
    return chunk;
}