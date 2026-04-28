#ifndef LKM_API_H
#define LKM_API_H
#include <linux/types.h>

int map_shadow_stack(unsigned long vaddr);

void unmap_shadow_stack(pid_t pid);

#endif