#include "lkm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <unistd.h>

static struct ss_chunk* shalloc(unsigned long vaddr) {
    int fd = open("/dev/shadowstack", O_RDWR);
    if (fd < 0) {
        perror("open");
        return NULL;
    }

    struct ioctl_params req;
    req.vaddr = vaddr;

    long err = ioctl(fd, IOCTL_SHADOW_REQ, &req);
    if (err < 0) {
        perror("ioctl");
        close(fd);
        return NULL;
    }
    close(fd);
    
    struct ss_chunk *chunk = malloc(sizeof(struct ss_chunk));
    memcpy(chunk, &req.chunk, sizeof(*chunk));
    printf("got chunk %ln\n", chunk->top);
    return chunk;
}

int main(int argc, const char *argv[]) {
    uint64_t vaddr = 0xfffffdffffffffff;
    struct ss_chunk *chunk = shalloc(vaddr);
    sprintf(chunk->top, "%d", 42);
    int n;
    sscanf(chunk->top, "%s", &n);
    printf("%d\n", n);
}