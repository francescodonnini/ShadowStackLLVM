#ifndef LKM_API_H
#define LKM_API_H
#include <linux/atomic.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/mm_types.h>
#include <linux/rwsem.h>
#include <linux/sched/mm.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#define SS_OFFSET  0xFFFFD88000000000ULL - 0x0000600000000000ULL
#define SS_START   (0xffffeb0000000000ULL)
#define SS_END     (0xfffffc0000000000ULL)
#define SS_SIZE    (1024 * 1024)

struct sa_allocator_desc {
    pid_t                tgid;
    struct mm_struct    *mm;
    atomic64_t           free_area;
    struct rw_semaphore  al_lock;
    struct list_head     active_list;
    spinlock_t           fl_lock;
    struct list_head     free_list;
    struct kref          kref;
};

struct sa_allocator_desc* alloc_create(pid_t tgid, struct mm_struct *mm);

void alloc_destroy(struct sa_allocator_desc *alloc);

long sa_alloc(uint64_t *vaddr);

long sa_free(uint64_t usr_addr);

long sa_fork(pid_t p_tgid, pid_t p_pid);

long sa_pivot(uint64_t old_stack, uint64_t new_stack);

long resolve_symbols(void);

void vma_free(void);

#endif