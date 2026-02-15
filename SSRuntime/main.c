#define _GNU_SOURCE
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <asm/prctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#define SHADOW_AREA_SIZE (100 * 4096)

typedef struct ShadowArea {
    uint64_t sa_size;
    uint64_t sa_ra[];
} ShadowArea;

__attribute__((constructor))
void ShadowStackSetup(void) {
    ShadowArea *sa = (ShadowArea *)mmap(NULL, SHADOW_AREA_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (sa == MAP_FAILED) {
        perror("[ShadowStack] mmap failed");
        exit(1);
    }
    sa->sa_size = 0;

    if (syscall(SYS_arch_prctl, ARCH_SET_GS, (unsigned long)sa) != 0) {
        perror("[ShadowStack] arch_prctl failed");
        exit(1);
    }

}