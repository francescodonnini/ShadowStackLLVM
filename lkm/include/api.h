#ifndef LKM_API_H
#define LKM_API_H
#include <linux/types.h>

long sa_alloc(uint64_t *vaddr);

long resolve_symbols(void);

#endif