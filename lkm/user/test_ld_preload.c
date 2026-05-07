#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void* worker_thread(void* arg) {
    long thread_id = (long)arg;
    printf("[Thread %ld] Running normally. If the wrapper works, I have a shadow stack!\n", thread_id);
    usleep(10000); 
    return NULL;
}

int main(void) {
    pthread_t threads[5];

    printf("[Main] Starting naive multithreaded app...\n");

    for (long i = 0; i < 5; i++) {
        pthread_create(&threads[i], NULL, worker_thread, (void*)i);
    }

    for (int i = 0; i < 5; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("[Main] Exiting cleanly.\n");
    return EXIT_SUCCESS;
}