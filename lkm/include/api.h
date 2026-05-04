#ifndef LKM_API_H
#define LKM_API_H
#include <linux/types.h>

long sa_alloc(uint64_t *vaddr);

long sa_free(uint64_t usr_addr);

long resolve_symbols(void);

#endif