#include "api.h"
#include <asm/cpufeature.h>
#include <asm/pgtable_types.h>
#include <asm/tlbflush.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/pgtable.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#define SHADOW_SIZE     (1 * 1024 * 1024)
#define SHADOW_PT_FLAGS (_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY)

struct sa_desc {
    pid_t pid;
    void *kaddr;
    uint64_t vaddr;
    unsigned long size;
    struct list_head list;
};

static LIST_HEAD(sa_list);
static DEFINE_MUTEX(sa_mutex);

static unsigned long alloc_pt_page(void) {
    return __get_free_page(GFP_KERNEL | __GFP_ZERO);
}

static int map_page(pgd_t *user_pgd, unsigned long vaddr, unsigned long phy) {
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;

    pgd = user_pgd + pgd_index(vaddr);
    if (pgd_none(*pgd)) {
        unsigned long tbl_page = alloc_pt_page();
        if (!tbl_page) return -ENOMEM;
        set_pgd(pgd, __pgd(__pa(tbl_page) | SHADOW_PT_FLAGS));
    }

    p4d = p4d_offset(pgd, vaddr);
    if (p4d_none(*p4d)) {
        unsigned long tbl_page = alloc_pt_page();
        if (!tbl_page) return -ENOMEM;
        set_p4d(p4d, __p4d(__pa(tbl_page) | SHADOW_PT_FLAGS));
    }

    pud = pud_offset(p4d, vaddr);
    if (pud_none(*pud)) {
        unsigned long tbl_page = alloc_pt_page();
        if (!tbl_page) return -ENOMEM;
        set_pud(pud, __pud(__pa(tbl_page) | SHADOW_PT_FLAGS));
    }

    pmd = pmd_offset(pud, vaddr);
    if (pmd_none(*pmd)) {
        unsigned long tbl_page = alloc_pt_page();
        if (!tbl_page) return -ENOMEM;
        set_pmd(pmd, __pmd(__pa(tbl_page) | SHADOW_PT_FLAGS));
    }

    pte = pte_offset_kernel(pmd, vaddr);
    set_pte(pte, __pte(phy | SHADOW_PT_FLAGS));

    __flush_tlb_one_user(vaddr);

    return 0;
}

static pgd_t *get_current_user_pgd(void) {
    struct mm_struct *mm;
    
    mm = current->mm;
    if (!mm) {
        return NULL;
    }
    return mm->pgd;
}

struct ss_chunk* map_shadow_stack(unsigned long vaddr) {
    pgd_t *user_pgd;
    struct ss_chunk *mem;
    unsigned long offset;
    struct sa_desc *desc;

    user_pgd = get_current_user_pgd();
    if (!user_pgd) {
        return ERR_PTR(-EINVAL);
    }

    mem = vzalloc(SHADOW_SIZE);
    if (!mem) {
        return ERR_PTR(-ENOMEM);
    }
    mem->top = mem->stack;

    for (offset = 0; offset < SHADOW_SIZE; offset += PAGE_SIZE) {
        struct page *page = vmalloc_to_page((void*)mem + offset);
        unsigned long phy = page_to_phys(page);
        int err = map_page(user_pgd, vaddr + offset, phy);
        if (err) {
            vfree(mem);
            return ERR_PTR(err);
        }
    }
    
    desc = kmalloc(sizeof(*desc), GFP_KERNEL);
    if (desc) {
        desc->pid = current->pid;
        desc->vaddr = vaddr;
        desc->kaddr = mem;
        desc->size = SHADOW_SIZE;

        mutex_lock(&sa_mutex);
        list_add(&desc->list, &sa_list);
        mutex_unlock(&sa_mutex);
    }

    return mem;
}

static void unmap_shadow_pt(pgd_t *user_pgd, unsigned long vaddr) {
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;
    
    pgd = user_pgd + pgd_index(vaddr);
    if (pgd_none(*pgd)) return;

    p4d = p4d_offset(pgd, vaddr);
    if (p4d_none(*p4d)) return;

    pud = pud_offset(p4d, vaddr);
    if (pud_none(*pud)) return;

    pmd = pmd_offset(pud, vaddr);
    if (pmd_none(*pmd)) return;
    
    pte = pte_offset_kernel(pmd, vaddr);
    if (pte_none(*pte)) return;

    pte_clear(NULL, vaddr, pte);
}

void unmap_shadow_stack(pid_t pid) {
    pgd_t *user_pgd;
    struct sa_desc *mapping;
    struct sa_desc *tmp;
    
    user_pgd = get_current_user_pgd();

    mutex_lock(&sa_mutex);
    list_for_each_entry_safe(mapping, tmp, &sa_list, list) {
        if (mapping->pid == pid) {
            if (user_pgd) {
                unmap_shadow_pt(user_pgd, mapping->vaddr);
            }
            vfree((void *)mapping->kaddr);
            list_del(&mapping->list);
            kfree(mapping);
        }
    }
    mutex_unlock(&sa_mutex);
}