#ifndef LKM_API_H
#define LKM_API_H
#include <linux/mm_types.h>
#include <linux/types.h>
#define SS_START     (0xffffeb0000000000ULL)
#define SS_END       (0xfffffc0000000000ULL)
#define SS_SIZE      (1024 * 1024)

struct sa_allocator_desc* alloc_create(pid_t tgid, struct mm_struct *mm);

void alloc_destroy(struct sa_allocator_desc *alloc);

long sa_alloc(uint64_t *vaddr);

long sa_free(uint64_t usr_addr);

long sa_fork(pid_t p_tgid, pid_t p_pid);

long resolve_symbols(void);

#endif