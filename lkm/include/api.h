#ifndef LKM_API_H
#define LKM_API_H
#include <linux/types.h>

struct ss_chunk {
    struct ss_chunk *padding;
    uint64_t        *top;
    uint64_t         data[];
};

struct ss_chunk* map_shadow_stack(unsigned long vaddr);

void unmap_shadow_stack(pid_t pid);

#endif