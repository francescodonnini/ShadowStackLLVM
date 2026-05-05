#ifndef LK_PT_BINDINGS_H
#define LK_PT_BINDINGS_H
#include <linux/mm.h>
#include <linux/version.h>

typedef int    (*__p4d_alloc_t)(struct mm_struct*,pgd_t*,unsigned long);
typedef int    (*__pud_alloc_t)(struct mm_struct*,p4d_t*,unsigned long);
typedef int    (*__pmd_alloc_t)(struct mm_struct*,pud_t*,unsigned long);
typedef int    (*__pte_alloc_t)(struct mm_struct*,pmd_t*);
typedef void   (*__flush_tlb_mm_range_t)(struct mm_struct*,unsigned long,unsigned long,unsigned int,bool);
typedef pte_t* (*__pte_offset_map_lock_t)(struct mm_struct*, pmd_t*,unsigned long, spinlock_t **);

extern __p4d_alloc_t my__p4d_alloc;
extern __pud_alloc_t my__pud_alloc;
extern __pmd_alloc_t my__pmd_alloc;
extern __pte_alloc_t my__pte_alloc;
extern __flush_tlb_mm_range_t my__flush_tlb_mm_range;
extern __pte_offset_map_lock_t my__pte_offset_map_lock;

static inline p4d_t *my_p4d_alloc(struct mm_struct *mm, pgd_t *pgd, unsigned long address) {
    return (unlikely(pgd_none(*pgd)) && my__p4d_alloc(mm, pgd, address)) ? NULL : p4d_offset(pgd, address);
}

static inline pud_t *my_pud_alloc(struct mm_struct *mm, p4d_t *p4d, unsigned long address) {
    return (unlikely(p4d_none(*p4d)) && my__pud_alloc(mm, p4d, address)) ? NULL : pud_offset(p4d, address);
}

static inline pmd_t *my_pmd_alloc(struct mm_struct *mm, pud_t *pud, unsigned long address) {
    return (unlikely(pud_none(*pud)) && my__pmd_alloc(mm, pud, address)) ? NULL : pmd_offset(pud, address);
}

static inline int my_pte_alloc(struct mm_struct *mm, pmd_t *pmd) {
    return (unlikely(pmd_none(*(pmd))) && my__pte_alloc(mm, pmd));
}

static inline void my_flush_tlb_mm(struct mm_struct *mm) {
    my__flush_tlb_mm_range(mm, 0UL, -1UL, 0UL, true);
}

static inline pte_t *my_pte_offset_map_lock(struct mm_struct *mm, pmd_t *pmd, unsigned long addr, spinlock_t **ptlp) {
    return my__pte_offset_map_lock(mm, pmd, addr, ptlp);
}

static inline pte_t *my_pte_alloc_map_lock(struct mm_struct *mm, pmd_t *pmd, unsigned long addr, spinlock_t **ptlp) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    pte_t *pte;
    if (my_pte_alloc(mm, pmd)) {
        return NULL;
    }
	__cond_lock(*ptlp, pte = my_pte_offset_map_lock(mm, pmd, addr, ptlp));
	return pte;
#else
    return pte_alloc_map_lock(mm, pmd, addr, ptlp);
#endif
}

#endif