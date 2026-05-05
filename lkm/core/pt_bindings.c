#include "pt_bindings.h"
#include "api.h"
#include <linux/kprobes.h>

__p4d_alloc_t my__p4d_alloc;
__pud_alloc_t my__pud_alloc;
__pmd_alloc_t my__pmd_alloc;
__pte_alloc_t my__pte_alloc;
__pte_offset_map_lock_t my__pte_offset_map_lock;
__flush_tlb_mm_range_t my__flush_tlb_mm_range;


static struct kprobe kp = {
    .symbol_name = "kallsyms_lookup_name"
};

typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);

long resolve_symbols(void) {
    kallsyms_lookup_name_t ksyms_lookup;

    register_kprobe(&kp);
    ksyms_lookup = (kallsyms_lookup_name_t)kp.addr;
    unregister_kprobe(&kp);

    if (!ksyms_lookup) {
        pr_err("could not find kallsyms_lookup_name");
        return -1;
    }
    my__p4d_alloc = (__p4d_alloc_t) ksyms_lookup("__p4d_alloc");
    if (!my__p4d_alloc) return -1;
    my__pud_alloc = (__pud_alloc_t) ksyms_lookup("__pud_alloc");
    if (!my__pud_alloc) return -1;
    my__pmd_alloc = (__pmd_alloc_t) ksyms_lookup("__pmd_alloc");
    if (!my__pmd_alloc) return -1;
    my__pte_alloc = (__pte_alloc_t) ksyms_lookup("__pte_alloc");
    if (!my__pte_alloc) return -1;
    my__flush_tlb_mm_range = (__flush_tlb_mm_range_t) ksyms_lookup("flush_tlb_mm_range");
    if (!my__flush_tlb_mm_range) return -1;
    my__pte_offset_map_lock = (__pte_offset_map_lock_t) ksyms_lookup("__pte_offset_map_lock");
    if (!my__pte_offset_map_lock) return -1;
    return 0;
}
