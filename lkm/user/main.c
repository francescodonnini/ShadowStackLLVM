#include "lkm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <unistd.h>

static uint64_t shalloc(unsigned long vaddr) {
    int fd = open("/dev/shadowstack", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 0;
    }

    struct ioctl_params req;
    req.vaddr = vaddr;

    long err = ioctl(fd, IOCTL_SHADOW_REQ, &req);
    if (err < 0) {
        perror("ioctl");
        close(fd);
        return 0;
    }
    close(fd);
    
    printf("got chunk %ld\n", req.top);
    return req.top;
}

int main(int argc, const char *argv[]) {
    uint64_t vaddr = 0xfffffdffffffffff;
    uint64_t top = shalloc(vaddr);
    if (!top) {
        printf("shalloc failed");
    }
    sprintf((void*)top, "%d", 42);
    int n;
    sscanf((void*)top, "%d", &n);
    printf("%d\n", n);
}