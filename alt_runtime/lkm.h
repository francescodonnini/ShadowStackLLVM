#ifndef LKM_IOCTL_H
#define LKM_IOCTL_H
#include <stdint.h>
#include <sys/ioctl.h>

#define IOCTL_SHADOW_MAGIC 0xca
#define IOCTL_SHADOW_REQ   _IOWR(IOCTL_SHADOW_MAGIC, IOCTL_SHADOW_REQ_NO, struct ioctl_params)
#define IOCTL_SHADOW_FREE  _IOWR(IOCTL_SHADOW_MAGIC, IOCTL_SHADOW_FREE_NO, struct ioctl_params)

enum {
    IOCTL_SHADOW_REQ_NO = 0x70,
    IOCTL_SHADOW_FREE_NO = 0x71,
    IOCTL_SHADOW_MAX_NR
};

struct ioctl_params { 
    long error;
    unsigned long long addr;
};

struct ss_chunk {
    struct ss_chunk *padding;
    void            *top;
    uint8_t          data[];
};

struct ss_chunk* MemPoolAlloc(void);

#endif