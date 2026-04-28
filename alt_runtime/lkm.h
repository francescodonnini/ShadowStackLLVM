#ifndef LKM_IOCTL_H
#define LKM_IOCTL_H
#include <stdint.h>
#include <sys/ioctl.h>

#define IOCTL_SHADOW_MAGIC 0xca
#define IOCTL_SHADOW_REQ   _IOWR(IOCTL_SHADOW_MAGIC, IOCTL_SHADOW_REQ_NO, struct ioctl_params)

enum {
    IOCTL_SHADOW_REQ_NO = 0x70,
    IOCTL_SHADOW_MAX_NR
};

struct ss_chunk {
    struct ss_chunk *padding;
    uint64_t        *top;
}

struct ioctl_params {
    uint64_t        vaddr;
    struct ss_chunk chunk; 
    long            error;
};

struct ss_chunk* MemPoolAlloc(void);

#endif