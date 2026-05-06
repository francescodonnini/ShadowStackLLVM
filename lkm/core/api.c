#include "api.h"
#include "pt_bindings.h"
#include <asm/cpufeature.h>
#include <asm/pgalloc.h>
#include <asm/pgtable_types.h>
#include <asm/tlbflush.h>
#include <linux/atomic.h>
#include <linux/io.h>
#include <linux/kprobes.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mmap_lock.h>
#include <linux/mutex.h>
#include <linux/pgtable.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#define SS_START (0xffffeb0000000000ULL)
#define SS_END   (0xfffffc0000000000ULL)
#define SS_SIZE  (1024 * 1024)

struct gl_vma_desc {
    void             *kernel_addr;
    struct list_head  list;
};

static LIST_HEAD(global_vma_list);
static DEFINE_SPINLOCK(global_vma_lock);

struct sa_thread_desc {
    pid_t              tid;
    uint64_t           usr_addr;
    void              *kernel_addr;
    struct list_head   list;
};

struct sa_free_vaddr_desc {
    uint64_t         vaddr;
    struct list_head list;
};

struct sa_allocator_desc {
    pid_t            tgid;
    atomic64_t       free_area;
    spinlock_t       fl_lock;
    struct list_head active_list;
    struct list_head free_list;
    struct list_head list;
};

static LIST_HEAD(sa_list);
static DEFINE_SPINLOCK(sa_lock);

static void *gl_vmalloc(void) {
    struct gl_vma_desc *desc;
    void *kaddr;

    spin_lock(&global_vma_lock);
    desc = list_first_entry_or_null(&global_vma_list, struct gl_vma_desc, list);
    if (desc) {
        list_del(&desc->list);
        spin_unlock(&global_vma_lock);

        kaddr = desc->kernel_addr;
        kfree(desc);

        memset(kaddr, 0, SS_SIZE);
        return kaddr;
    }
    spin_unlock(&global_vma_lock);

    return vzalloc(SS_SIZE);
}

static void gl_free(void *kaddr) {
    struct gl_vma_desc *desc;

    desc = kmalloc(sizeof(*desc), GFP_KERNEL);
    if (!desc) {
        vfree(kaddr);
        return;
    }
    desc->kernel_addr = kaddr;
    spin_lock(&global_vma_lock);
    list_add(&desc->list, &global_vma_list);
    spin_unlock(&global_vma_lock);
}

static long map_ss(struct mm_struct *mm, uint64_t vaddr, phys_addr_t phy) {
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;
    spinlock_t *ptl;
    int err;

    pgd = pgd_offset(mm, vaddr);
    p4d = my_p4d_alloc(mm, pgd, vaddr);
    if (!p4d) {
        pr_err("p4d_alloc() failed");
        err = -ENOMEM;
        goto out;
    }
    pud = my_pud_alloc(mm, p4d, vaddr);
    if (!pud) {
        pr_err("pud_alloc() failed");
        err = -ENOMEM;
        goto out;
    }
    pmd = my_pmd_alloc(mm, pud, vaddr);
    if (!pmd) {
        pr_err("pmd_alloc() failed");
        err = -ENOMEM;
        goto out;
    }
    pte = my_pte_alloc_map_lock(mm, pmd, vaddr, &ptl);
    if (!pte) {
        pr_err("my_pte_offset_map_lock() failed");
        err = -ENOMEM;
        goto out;
    }

    set_pte_at(mm, vaddr, pte, pfn_pte(phy >> PAGE_SHIFT, PAGE_SHARED));
    pte_unmap_unlock(pte, ptl);
    err = 0;

out:
    return err;
}

static uint64_t vmalloc_to_phy(void *addr) {
    struct page *p;
    phys_addr_t phy;

    p = vmalloc_to_page(addr);
    if (!p) return 0;

    phy = page_to_phys(p);
    return phy + offset_in_page(addr);
}

static void alloc_add_t(struct sa_allocator_desc *alloc, struct sa_thread_desc *t) {
    spin_lock(&alloc->fl_lock);
    list_add(&t->list, &alloc->active_list);
    spin_unlock(&alloc->fl_lock);
}

static uint64_t alloc_get_vaddr(struct sa_allocator_desc *alloc) {
    struct sa_free_vaddr_desc *desc;
    uint64_t vaddr;

    spin_lock(&alloc->fl_lock);
    desc = list_first_entry_or_null(&alloc->free_list, struct sa_free_vaddr_desc, list);
    if (desc) {
        vaddr = desc->vaddr;
        list_del(&desc->list);
        spin_unlock(&alloc->fl_lock);
        kfree(desc);
        return vaddr;            
    }
    spin_unlock(&alloc->fl_lock);

    return atomic64_fetch_add(SS_SIZE, &alloc->free_area);
}

static struct sa_allocator_desc* alloc_get_or_creat(pid_t tgid) {
    struct sa_allocator_desc *it;
    struct sa_allocator_desc *alloc;
    struct sa_allocator_desc *new_alloc;

    new_alloc = kmalloc(sizeof(*alloc), GFP_KERNEL);
    if (new_alloc) {
        new_alloc->tgid = tgid;
        spin_lock_init(&new_alloc->fl_lock);
        atomic64_set(&new_alloc->free_area, SS_START);
        INIT_LIST_HEAD(&new_alloc->free_list);
        INIT_LIST_HEAD(&new_alloc->active_list);
    }

    alloc = NULL;
    spin_lock(&sa_lock);
    list_for_each_entry_rcu(it, &sa_list, list) {
        if (it->tgid == tgid) {
            alloc = it;
            break;
        }
    }
    if (!alloc) {
        if (!new_alloc) {
            spin_unlock(&sa_lock);
            return NULL;
        }
        list_add_rcu(&new_alloc->list, &sa_list);
        alloc = new_alloc;
    } else {
        kfree(new_alloc);
    }
    spin_unlock(&sa_lock);

    return alloc;
}

static void alloc_add_vaddr(struct sa_allocator_desc *alloc, uint64_t vaddr) {
    struct sa_free_vaddr_desc *desc;
    desc = kmalloc(sizeof(*desc), GFP_KERNEL);
    if (!desc) return;

    desc->vaddr = vaddr;

    spin_lock(&alloc->fl_lock);
    list_add(&desc->list, &alloc->free_list);
    spin_unlock(&alloc->fl_lock);
}

static void unmap_ss_page(struct mm_struct *mm, uint64_t vaddr) {
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *ptep;
    spinlock_t *ptl;

    pgd = pgd_offset(mm, vaddr);
    if (pgd_none(*pgd) || pgd_bad(*pgd)) return;
    
    p4d = p4d_offset(pgd, vaddr);
    if (p4d_none(*p4d) || p4d_bad(*p4d)) return;
    
    pud = pud_offset(p4d, vaddr);
    if (pud_none(*pud) || pud_bad(*pud)) return;
    
    pmd = pmd_offset(pud, vaddr);
    if (pmd_none(*pmd) || pmd_bad(*pmd)) return;

    ptep = my_pte_offset_map_lock(mm, pmd, vaddr, &ptl);
    if (!ptep) return;

    pte_clear(mm, vaddr, ptep);
    
    pte_unmap_unlock(ptep, ptl);
}

long sa_alloc(uint64_t *vaddr) {
    struct sa_thread_desc *t_desc;
    struct sa_allocator_desc *allocator;
    long err;
    uint64_t usr_addr;
    void *kernel_addr;
    unsigned long offset;
    struct mm_struct *mm;

    mm = current->mm;
    if (!mm) return -EINVAL;

    t_desc = kmalloc(sizeof(*t_desc), GFP_KERNEL);
    if (!t_desc) return -ENOMEM;

    allocator = alloc_get_or_creat(current->tgid);
    if (!allocator) {
        err = -ENOMEM;
        goto no_allocator;
    }

    usr_addr = alloc_get_vaddr(allocator);
    if (usr_addr >= SS_END) {
        err = -ENOMEM;
        goto no_usr_addr;
    }

    kernel_addr = gl_vmalloc();
    if (!kernel_addr) {
        err = -ENOMEM;
        goto no_gl_vmalloc;
    }

    mmap_write_lock(mm);
    for (offset = 0; offset < SS_SIZE; offset += PAGE_SIZE) {
        phys_addr_t phy = vmalloc_to_phy(kernel_addr + offset);

        err = map_ss(mm, usr_addr + offset, phy);
        if (err) {
            unsigned long rollback_off;
            for (rollback_off = 0; rollback_off < offset; rollback_off += PAGE_SIZE) {
                unmap_ss_page(mm, usr_addr + rollback_off);
            }
            my_flush_tlb_mm(mm);
            mmap_write_unlock(mm);

            err = -ENOMEM;
            goto no_map;
        }
    }
    mmap_write_unlock(mm);

    t_desc->tid = current->pid;
    t_desc->usr_addr = usr_addr;
    t_desc->kernel_addr = kernel_addr;

    alloc_add_t(allocator, t_desc);
    *vaddr = usr_addr;

    return 0;

no_map:
    gl_free(kernel_addr);
no_gl_vmalloc:
    alloc_add_vaddr(allocator, usr_addr);
no_usr_addr:
no_allocator:
    kfree(t_desc);
    return err;
}

long sa_free(uint64_t usr_addr){
    struct sa_allocator_desc *alloc;
    struct sa_thread_desc *t_desc, *tmp;
    struct mm_struct *mm = current->mm;
    bool found = false;
    unsigned long offset;

    if (!mm) return -EINVAL;

    alloc = alloc_get_or_creat(current->tgid);
    if (!alloc) return -EINVAL;

    spin_lock(&alloc->fl_lock);
    list_for_each_entry_safe(t_desc, tmp, &alloc->active_list, list) {
        if (t_desc->usr_addr == usr_addr) {
            list_del(&t_desc->list);
            found = true;
            break;
        }
    }
    spin_unlock(&alloc->fl_lock);

    if (!found) return -EINVAL;

    mmap_write_lock(mm);
    for (offset = 0; offset < SS_SIZE; offset += PAGE_SIZE) {
        unmap_ss_page(mm, usr_addr + offset);
    }
    my_flush_tlb_mm(mm);
    mmap_write_unlock(mm);

    gl_free(t_desc->kernel_addr);
    alloc_add_vaddr(alloc, usr_addr);
    kfree(t_desc);

    return 0;
}

static void sa_free_pte(struct mm_struct *mm, pte_t *pte_start, pmd_t *pmd) {
    pte_t *pte;
    int i;

    for (i = 0; i < PTRS_PER_PTE; i++) {
        pte = pte_start + i;
        if (!pte_none(ptep_get(pte)))
            return;
    }

    mm_dec_nr_ptes(mm);
    pte_free(mm, pmd_page(*pmd));
    pmd_clear(pmd);
} 

static void sa_free_pmd(struct mm_struct *mm, pmd_t *pmd_start, pud_t *pud) {
    pmd_t *pmd;
    int i;

    for (i = 0; i < PTRS_PER_PMD; i++) {
        pmd = pmd_start + i;
        if (!pmd_none(*pmd))
            return;
    }

    mm_dec_nr_pmds(mm);
    pmd_free(mm, (pmd_t*)page_to_virt(pud_page(*pud)));
    pud_clear(pud);
} 

static void sa_free_pud(struct mm_struct *mm, pud_t *pud_start, p4d_t *p4d) {
    pud_t *pud;
    int i;

    for (i = 0; i < PTRS_PER_PUD; i++) {
        pud = pud_start + i;
        if (!pud_none(*pud))
            return;
    }

    mm_dec_nr_puds(mm);
    pud_free(mm, (pud_t*)page_to_virt(p4d_page(*p4d)));
    p4d_clear(p4d);
}

static void sa_free_p4d(struct mm_struct *mm, p4d_t *p4d_start, pgd_t *pgd) {
    p4d_t *p4d;
    int i;

    for (i = 0; i < PTRS_PER_P4D; i++) {
        p4d = p4d_start + i;
        if (!p4d_none(*p4d))
            return;
    }

    mm_dec_nr_p4ds(mm);
    p4d_free(mm, (p4d_t*)page_to_virt(pgd_page(*pgd)));
    pgd_clear(pgd);
}

static void sa_remove_pte(struct mm_struct *mm, pte_t *pte, unsigned long addr, unsigned long end) {
    unsigned long next;
    pte_t ptent;

    for (; addr < end; addr = next, pte++) {
        next = (addr + PAGE_SIZE) & PAGE_MASK;
        if (next > end)
            next = end;
        
        ptent = ptep_get(pte);
        if (!pte_present(ptent))
            continue;
        
        pte_clear(mm, addr, pte);
    }
}

static void sa_remove_pmd(struct mm_struct *mm, pmd_t *pmd, unsigned long addr, unsigned long end) {
    unsigned long next;

    for (; addr < end; addr = next, pmd++) {
        pte_t *pte;

        next = pmd_addr_end(addr, end);
        if (!pmd_present(*pmd))
            continue;

        pte = pte_offset_kernel(pmd, addr);
        sa_remove_pte(mm, pte, addr, next);
        sa_free_pte(mm, pte_offset_kernel(pmd, 0), pmd);
    }
}

static void sa_remove_pud(struct mm_struct *mm, pud_t *pud, unsigned long addr, unsigned long end) {
    unsigned long next;

    for (; addr < end; addr = next, pud++) {
        pmd_t *pmd;
        pmd_t *pmd_base;

        next = pud_addr_end(addr, end);
        if (!pud_present(*pud))
            continue;

        pmd = pmd_offset(pud, addr);
        pmd_base = pmd_offset(pud, 0);
        sa_remove_pmd(mm, pmd, addr, next);
        sa_free_pmd(mm, pmd_base, pud);
    }
}

static void sa_remove_p4d(struct mm_struct *mm, p4d_t *p4d, unsigned long addr, unsigned long end) {
    unsigned long next;

    for (; addr < end; addr = next, p4d++) {
        pud_t *pud;

        next = p4d_addr_end(addr, end);
        if (!p4d_present(*p4d))
            continue;

        pud = pud_offset(p4d, addr);
        sa_remove_pud(mm, pud, addr, next);
        sa_free_pud(mm, pud_offset(p4d, 0), p4d);
    }
}

static void sa_remove(struct mm_struct *mm, unsigned long start, unsigned long end) {
    unsigned long addr;
    unsigned long next;
    pgd_t *pgd;

    for (addr = start; addr < end; addr = next) {
        p4d_t *p4d;

        next = pgd_addr_end(addr, end);
        pgd = pgd_offset(mm, addr);
        if (!pgd_present(*pgd))
            continue;

        p4d = p4d_offset(pgd, addr);
        sa_remove_p4d(mm, p4d, addr, next);
        sa_free_p4d(mm, p4d_offset(pgd, 0), pgd);
    }
}

long sa_tdown(struct mm_struct *mm) {
    mmap_write_lock(mm);
    sa_remove(mm, SS_START, SS_END);
    my_flush_tlb_mm(mm);
    mmap_write_unlock(mm);
    return 0;
}