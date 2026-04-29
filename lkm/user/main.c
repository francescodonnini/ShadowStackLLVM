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
    
    printf("got chunk %ln\n", req.top);
    return req.top;
}

int main(int argc, const char *argv[]) {
    uint64_t vaddr = 0xfffffdffffffffff;
    uint64_t top = shalloc(vaddr);
    sprintf(top, "%d", 42);
    int n;
    sscanf(top, "%s", &n);
    printf("%d\n", n);
}