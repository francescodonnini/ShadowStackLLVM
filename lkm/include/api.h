#ifndef LKM_API_H
#define LKM_API_H
#include <linux/mm_types.h>
#include <linux/types.h>

long sa_alloc(uint64_t *vaddr);

long sa_free(uint64_t usr_addr);

long sa_tdown(struct mm_struct *mm);

long resolve_symbols(void);

#endif