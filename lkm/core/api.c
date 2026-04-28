#include "api.h"
#include <asm/cpufeature.h>
#include <asm/pgtable_types.h>
#include <asm/tlbflush.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/pgtable.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#define SHADOW_SIZE     (1 * 1024 * 1024)
#define SHADOW_PT_FLAGS (_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY)
struct sa_desc {
    pid_t pid;
    unsigned long vaddr;
    unsigned long size;
    struct list_head list;
};

static LIST_HEAD(sa_list);
static DEFINE_MUTEX(sa_mutex);

static unsigned long alloc_pt_page(void) {
    return __get_free_page(GFP_KERNEL | __GFP_ZERO);
}

static int map_page(pgd_t *user_pgd, unsigned long vaddr, unsigned long phy) {
    pgd_t *pgd = user_pgd + pgd_index(vaddr);
    if (pgd_none(*pgd)) {
        unsigned long tbl_page = alloc_pt_page();
        if (!tbl_page) return -ENOMEM;
        set_pgd(pgd, __pgd(__pa(tbl_page) | SHADOW_PT_FLAGS));
    }

    p4d_t *p4d = p4d_offset(pgd, vaddr);
    if (p4d_none(*p4d)) {
        unsigned long tbl_page = alloc_pt_page();
        if (!tbl_page) return -ENOMEM;
        set_p4d(p4d, __p4d(__pa(tbl_page) | SHADOW_PT_FLAGS));
    }

    pud_t *pud = pud_offset(p4d, vaddr);
    if (pud_none(*pud)) {
        unsigned long tbl_page = alloc_pt_page();
        if (!tbl_page) return -ENOMEM;
        set_pud(pud, __pud(__pa(tbl_page) | SHADOW_PT_FLAGS));
    }

    pmd_t *pmd = pmd_offset(pud, vaddr);
    if (pmd_none(*pmd)) {
        unsigned long tbl_page = alloc_pt_page();
        if (!tbl_page) return -ENOMEM;
        set_pmd(pmd, __pmd(__pa(tbl_page) | SHADOW_PT_FLAGS));
    }

    pte_t *pte = pte_offset_kernel(pmd, vaddr);
    set_pte(pte, __pte(phy | SHADOW_PT_FLAGS));

    __flush_tlb_one_user(vaddr);

    return 0;
}

static pgd_t *get_current_user_pgd(void) {
    struct mm_struct *mm = current->mm;
    if (!mm || !boot_cpu_has(X86_FEATURE_PTI)) {
        return NULL;
    }
    return (pgd_t *)((unsigned long)mm->pgd | PAGE_SIZE);
}

int map_shadow_stack(unsigned long vaddr) {
    pgd_t *user_pgd = get_current_user_pgd();
    if (!user_pgd) {
        return -EINVAL;
    }

    void *mem = vzalloc(SHADOW_SIZE);
    if (!mem) {
        return -ENOMEM;
    }

    for (unsigned long offset = 0; offset < SHADOW_SIZE; offset += PAGE_SIZE) {
        struct page *page = vmalloc_to_page(mem + offset);
        unsigned long phy = page_to_phys(page);
        int err = map_page(user_pgd, vaddr + offset, phy);
        if (err) {
            vfree(mem);
            return err;
        }
    }
    
    struct sa_desc *desc;
    desc = kmalloc(sizeof(*desc), GFP_KERNEL);
    if (desc) {
        desc->pid = current->pid;
        desc->vaddr = vaddr;
        desc->size = SHADOW_SIZE;

        mutex_lock(&sa_mutex);
        list_add(&desc->list, &sa_list);
        mutex_unlock(&sa_mutex);
    }


    return 0;
}

static void unmap_shadow_pt(pgd_t *user_pgd, unsigned long vaddr) {
    pgd_t *pgd = user_pgd + pgd_index(vaddr);
    if (pgd_none(*pgd)) return;

    p4d_t *p4d = p4d_offset(pgd, vaddr);
    if (p4d_none(*p4d)) return;

    pud_t *pud = pud_offset(p4d, vaddr);
    if (pud_none(*pud)) return;

    pmd_t *pmd = pmd_offset(pud, vaddr);
    if (pmd_none(*pmd)) return;
    
    pte_t *pte = pte_offset_kernel(pmd, vaddr);
    if (pte_none(*pte)) return;

    pte_clear(NULL, vaddr, pte);
}

void unmap_shadow_stack(pid_t pid) {
    pgd_t *user_pgd = get_current_user_pgd();

    mutex_lock(&sa_mutex);
    struct sa_desc *mapping, *tmp;
    list_for_each_entry_safe(mapping, tmp, &sa_list, list) {
        if (mapping->pid == pid) {
            if (user_pgd) {
                unmap_shadow_pt(user_pgd, mapping->vaddr);
            }
            vfree((void *)mapping->vaddr);
            list_del(&mapping->list);
            kfree(mapping);
        }
    }
    mutex_unlock(&sa_mutex);
}