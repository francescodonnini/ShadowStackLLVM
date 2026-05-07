#include "api.h"
#include "pt_bindings.h"
#include <linux/atomic.h>
#include <linux/io.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/printk.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/xarray.h>
#define VMA_CAPACITY (128)

struct gl_vma_desc {
    void             *kernel_addr;
    struct list_head  list;
};

static int global_vma_list_size = 0;
static LIST_HEAD(global_vma_list);
static DEFINE_SPINLOCK(global_vma_lock);

struct sa_thread_desc {
    pid_t              tid;
    uint64_t           usr_addr;
    void              *kernel_addr;
    struct list_head   list;
};

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

static DEFINE_XARRAY(sa_allocators);

static void *gl_vmalloc(void) {
    struct gl_vma_desc *desc;
    void *kaddr;

    spin_lock(&global_vma_lock);
    desc = list_first_entry_or_null(&global_vma_list, struct gl_vma_desc, list);
    if (desc) {
        global_vma_list_size--;
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

    if (global_vma_list_size >= VMA_CAPACITY) {
        vfree(kaddr);
        return;
    }
    desc = kmalloc(sizeof(*desc), GFP_KERNEL);
    if (!desc) {
        vfree(kaddr);
        return;
    }
    desc->kernel_addr = kaddr;
    spin_lock(&global_vma_lock);
    global_vma_list_size++;
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
    down_write(&alloc->al_lock);
    list_add(&t->list, &alloc->active_list);
    up_write(&alloc->al_lock);
}

static uint64_t alloc_get_free_vaddr(struct sa_allocator_desc *alloc) {
    struct sa_thread_desc *desc;
    uint64_t vaddr;

    spin_lock(&alloc->fl_lock);
    desc = list_first_entry_or_null(&alloc->free_list, struct sa_thread_desc, list);
    if (desc) {
        vaddr = desc->usr_addr;
        list_del(&desc->list);
        spin_unlock(&alloc->fl_lock);
        kfree(desc);
        return vaddr;            
    }
    spin_unlock(&alloc->fl_lock);

    return atomic64_fetch_add(SS_SIZE, &alloc->free_area);
}

static void alloc_freelist_add(struct sa_allocator_desc *alloc, struct list_head *list) {
    spin_lock(&alloc->fl_lock);
    list_add(list, &alloc->free_list);
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

static void alloc_destroy_callback(struct kref *kref) {
    struct sa_allocator_desc *alloc;
    struct sa_thread_desc *t_desc;
    struct sa_thread_desc *t_tmp;
    
    alloc = container_of(kref, struct sa_allocator_desc, kref);

    list_for_each_entry_safe(t_desc, t_tmp, &alloc->active_list, list) {
        list_del(&t_desc->list);
        gl_free(t_desc->kernel_addr); 
        kfree(t_desc);
    }

    list_for_each_entry_safe(t_desc, t_tmp, &alloc->free_list, list) {
        list_del(&t_desc->list);
        kfree(t_desc);
    }

    kfree(alloc);
}

static void alloc_put(struct sa_allocator_desc *alloc) {
    kref_put(&alloc->kref, alloc_destroy_callback);
}

struct sa_allocator_desc* alloc_create(pid_t tgid, struct mm_struct *mm) {
    struct sa_allocator_desc *alloc;
    long err;

    alloc = kmalloc(sizeof(*alloc), GFP_KERNEL);
    if (!alloc) return NULL;

    alloc->tgid = tgid;
    alloc->mm = mm;
    init_rwsem(&alloc->al_lock);
    spin_lock_init(&alloc->fl_lock);
    atomic64_set(&alloc->free_area, SS_START);
    INIT_LIST_HEAD(&alloc->free_list);
    INIT_LIST_HEAD(&alloc->active_list);
    kref_init(&alloc->kref);

    err = xa_insert(&sa_allocators, tgid, alloc, GFP_KERNEL);
    if (err) {
        kfree(alloc);
        if (err == -EBUSY) {
            pr_err("Allocator for TGID %d does already exists", tgid);
        }
        return NULL;
    }

    kref_get(&alloc->kref);
    return alloc;
}

void alloc_destroy(struct sa_allocator_desc *alloc) {
    if (!alloc) return;

    xa_erase(&sa_allocators, alloc->tgid);
    alloc_put(alloc);
}

static struct sa_allocator_desc *alloc_get(pid_t tgid) {
    struct sa_allocator_desc *alloc;

    rcu_read_lock();
    alloc = xa_load(&sa_allocators, tgid);
    if (alloc && kref_get_unless_zero(&alloc->kref)) {
        rcu_read_unlock();
        return alloc;
    }
    rcu_read_unlock();
    return NULL;
}

long sa_alloc(uint64_t *vaddr) {
    struct sa_thread_desc *t_desc;
    struct sa_allocator_desc *alloc;
    long err;
    uint64_t usr_addr;
    void *kernel_addr;
    unsigned long offset;
    struct mm_struct *mm;

    mm = current->mm;
    if (!mm) return -EINVAL;

    t_desc = kmalloc(sizeof(*t_desc), GFP_KERNEL);
    if (!t_desc) return -ENOMEM;

    alloc = alloc_get(current->tgid);
    if (!alloc) {
        kfree(t_desc);
        return -ENOMEM;
    }

    usr_addr = alloc_get_free_vaddr(alloc);
    if (usr_addr >= SS_END) {
        alloc_put(alloc);
        return -ENOMEM;
    }

    kernel_addr = gl_vmalloc();
    if (!kernel_addr) {
        kfree(t_desc);
        alloc_put(alloc);
        return -ENOMEM;
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

            alloc_freelist_add(alloc, &t_desc->list);
            alloc_put(alloc);
            return -ENOMEM;
        }
    }
    mmap_write_unlock(mm);

    t_desc->tid = current->pid;
    t_desc->usr_addr = usr_addr;
    t_desc->kernel_addr = kernel_addr;

    alloc_add_t(alloc, t_desc);
    *vaddr = usr_addr;

    return 0;
}

long sa_free(uint64_t usr_addr){
    struct sa_allocator_desc *alloc;
    struct sa_thread_desc *t_desc, *tmp;
    struct mm_struct *mm = current->mm;
    bool found = false;
    unsigned long offset;

    if (!mm) return -EINVAL;

    alloc = alloc_get(current->tgid);
    if (!alloc) return -EINVAL;

    down_write(&alloc->al_lock);
    list_for_each_entry_safe(t_desc, tmp, &alloc->active_list, list) {
        if (t_desc->usr_addr == usr_addr) {
            list_del(&t_desc->list);
            found = true;
            break;
        }
    }
    up_write(&alloc->al_lock);

    if (!found) {
        alloc_put(alloc);
        return -EINVAL;
    }

    mmap_write_lock(mm);
    for (offset = 0; offset < SS_SIZE; offset += PAGE_SIZE) {
        unmap_ss_page(mm, usr_addr + offset);
    }
    my_flush_tlb_mm(mm);
    mmap_write_unlock(mm);

    gl_free(t_desc->kernel_addr);
    alloc_freelist_add(alloc, &t_desc->list);
    kfree(t_desc);
    alloc_put(alloc);

    return 0;
}

static long sa_copy(
    pid_t p_tgid,
    pid_t p_pid,
    struct sa_allocator_desc *c_alloc,
    struct sa_thread_desc *c_t) {
    struct sa_allocator_desc *alloc;
    struct sa_thread_desc *t_it;
    bool found;

    alloc = alloc_get(p_tgid);
    if (!alloc) {
        return -ENOENT;
    }

    found = false;
    down_read(&alloc->al_lock);
    list_for_each_entry(t_it, &alloc->active_list, list) {
        if (t_it->tid == p_pid) {
            memcpy(c_t->kernel_addr, t_it->kernel_addr, SS_SIZE);
            c_t->usr_addr = t_it->usr_addr;
            found = true;
            break;
        }
    }
    up_read(&alloc->al_lock);

    alloc_put(alloc);
    if (!found) {
        return -ENOENT;
    }
    atomic64_set(&c_alloc->free_area, c_t->usr_addr);
    alloc_add_t(c_alloc, c_t);

    return 0;
}

long sa_fork(pid_t p_tgid, pid_t p_pid) {
    struct mm_struct *child_mm;
    struct sa_thread_desc *t_desc;
    struct sa_allocator_desc *c_alloc;
    void *c_stack;
    long err;
    unsigned long offset;

    child_mm = current->mm;
    if (!child_mm) return -EINVAL;

    t_desc = kmalloc(sizeof(*t_desc), GFP_KERNEL);
    if (!t_desc) return -ENOMEM;

    c_alloc = alloc_get(current->tgid);
    if (!c_alloc) {
        err = -ENOMEM;
        goto no_alloc;
    }

    c_stack = gl_vmalloc();
    if (!c_stack) {
        err = -ENOMEM;
        goto no_vmalloc;
    }

    t_desc->kernel_addr = c_stack;
    t_desc->tid = current->pid;

    err = sa_copy(p_tgid, p_pid, c_alloc, t_desc);
    if (err < 0) {
        goto no_parent;
    }

    mmap_write_lock(child_mm);
    for (offset = 0; offset < SS_SIZE; offset += PAGE_SIZE) {
        phys_addr_t phy = vmalloc_to_phy(t_desc->kernel_addr + offset);

        err = map_ss(child_mm, t_desc->usr_addr + offset, phy);
        if (err) {
            unsigned long rollback_off;
            for (rollback_off = 0; rollback_off < offset; rollback_off += PAGE_SIZE) {
                unmap_ss_page(child_mm, t_desc->usr_addr + rollback_off);
            }
            my_flush_tlb_mm(child_mm);
            mmap_write_unlock(child_mm);

            err = -ENOMEM;
            goto no_map;
        }
    }
    mmap_write_unlock(child_mm);
    return 0;

no_map:
    down_write(&c_alloc->al_lock);
    list_del(&t_desc->list);
    up_write(&c_alloc->al_lock);
no_parent:
    gl_free(c_stack);
no_vmalloc:
    alloc_put(c_alloc);
no_alloc:
    kfree(t_desc);
    return err;
}