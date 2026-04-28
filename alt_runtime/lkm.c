#include "lkm.h"

struct ss_chunk* shalloc(unsigned long vaddr) {
    int fd = open("/dev/shadow_stack", O_RDWR);
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
        return -1;
    }

    close(fd);
    return req.;
}