#include "mapping.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

typedef struct MemArea {
    struct MemArea *next;
    uint64_t lo;
    uint64_t hi;
} MemArea;

static int insert_ma(MemArea **list, uint64_t lo, uint64_t hi) {
    MemArea *a = malloc(sizeof(MemArea));
    if (!a) return -1;
    a->lo = lo;
    a->hi = hi;
    a->next = *list;
    *list = a;
    return 0;
}

static int reserve_all(MemArea *list, uint64_t target_lo, uint64_t target_hi) {
    for (MemArea *p = list; p; p = p->next) {
        if (p->lo >= target_hi || p->hi <= target_lo) {
            void *addr = mmap((void*)p->lo, p->hi - p->lo, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            if (addr == MAP_FAILED) {
                perror("reserve_all");
                return -1;
            }
        }
    }
    return 0;
}

int setup_memory(uint64_t *lo_out, uint64_t *hi_out, uint64_t min) {
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) {
        return -1;
    }

    const int bufsz = 8192;
    char *line = malloc(bufsz);
    if (!line) {
        goto out;
    }

    const uint64_t BASE = 0x700000000000;
    const uint64_t CEIL = 0x7fffffffffff;

    uint64_t prev_hi = BASE;
    MemArea *list = NULL;
    char *p;
    while ((p = fgets(line, bufsz - 1, fp))) {
        char *endp;
        uint64_t lo = strtoull(p, &endp, 16);
        if (!lo || *endp != '-') goto out2;
        if (lo > CEIL) continue; // skip vsyscall 
        p = ++endp;
        uint64_t hi = strtoull(p, &endp, 16);
        if (!hi || (*endp != ' ' && *endp != '\t')) goto out2;

        if (lo > prev_hi) {
            if (insert_ma(&list, prev_hi, lo)) goto out2;
        }

        if (hi > prev_hi) prev_hi = hi;
    }

    if (prev_hi < CEIL) {
        if (insert_ma(&list, prev_hi, CEIL)) goto out2;
    }

    bool found = false;
    uint64_t target_lo, target_hi;
    for (MemArea *p = list; p; p = p->next) {
        uint64_t size = p->hi - p->lo;
        if (size >= min) {
            target_lo = p->lo;
            target_hi = p->hi;
            found = true;
            break;
        }
    }
    if (found) {
        if (target_hi - target_lo > min) {
            target_lo = target_hi - min;
        }
        target_lo &= ~15;
        target_hi &= ~15;
        if (reserve_all(list, target_lo, target_hi)) {
            found = false;
            goto out2;
        }
        *lo_out = target_lo;
        *hi_out = target_hi;
    }

out2:
    free(line);
    for (MemArea *p = list; p;) {
        MemArea *t = p->next;
        free(p);
        p = t;
    }
out:
    fclose(fp);
    return found ? 0 : -1;
}

int main() {
    uint64_t lo, hi;
    setup_memory(&lo, &hi, 4096);
    printf("Selected area: %lx - %lx\n", lo, hi);
}