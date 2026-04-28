#ifndef LKM_IOCTL_H
#define LKM_IOCTL_H
#include <asm/ioctl.h>
#include <linux/types.h>

#define IOCTL_SHADOW_MAGIC 0xca
#define IOCTL_SHADOW_REQ   _IOWR(IOCTL_SHADOW_MAGIC, IOCTL_SHADOW_REQ_NO, struct ioctl_params)

enum {
    IOCTL_SHADOW_REQ_NO = 0x70,
    IOCTL_SHADOW_MAX_NR
};

struct ioctl_params {
    uint64_t vaddr;
    uint64_t top; 
    long     error;
};
#endif