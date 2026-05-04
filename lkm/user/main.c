#include "lkm.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

#define NUM_THREADS 10 // Increase this to 50 or 100 for a real stress test!

// We can share the file descriptor across threads safely.
// The ioctl syscall will handle concurrent entries into your kernel module.
int fd;

// The worker function for each thread
void* worker_thread(void* arg) {
    long thread_id = (long)arg;
    struct ioctl_params req = { .error = 0, .addr = 0 };

    // 1. Each thread independently requests an address
    if (ioctl(fd, IOCTL_SHADOW_REQ, &req) < 0) {
        perror("[Worker] ioctl failed");
        return NULL;
    }

    // Check for internal LKM errors
    if (req.error != 0) {
        fprintf(stderr, "[Thread %ld] LKM returned error: %ld\n", thread_id, req.error);
        return NULL;
    }

    printf("[Thread %ld] ioctl success! Allocated Address: 0x%llx\n", thread_id, req.addr);

    // 2. Cast the returned address so we can use it in user space
    char* my_stack = (char*)(uintptr_t)req.addr;

    // 3. Write unique data to the newly mapped page
    sprintf(my_stack, "Hello from Thread %ld!", thread_id);
    
    // Quick sleep to encourage thread interleaving
    usleep(10000); 

    // 4. Read it back to verify the page is mapped and strictly isolated
    printf("[Thread %ld] Data check: \"%s\"\n", thread_id, my_stack);

    return NULL;
}

int main(void) {
    pthread_t threads[NUM_THREADS];

    // 1. Open the character device once for the whole process
    fd = open("/dev/shadowstack", O_RDWR);
    if (fd < 0) {
        perror("Failed to open /dev/shadowstack");
        return EXIT_FAILURE;
    }

    printf("[Main] Device opened. Spawning %d threads to blast ioctl requests...\n", NUM_THREADS);

    // 2. Fire off all the threads
    for (long i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, worker_thread, (void*)i) != 0) {
            perror("Failed to create thread");
        }
    }

    // 3. Wait for all threads to finish their reads/writes
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    close(fd);
    printf("[Main] Stress test complete.\n");
    
    return EXIT_SUCCESS;
}