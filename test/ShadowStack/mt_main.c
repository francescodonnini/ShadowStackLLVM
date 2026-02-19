#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

pthread_mutex_t print_lock = PTHREAD_MUTEX_INITIALIZER;

__attribute__((noinline))
int add(int a, int b) {
    __asm__ (
        "addq $8, (%%rsp);" 
        : 
        : 
        : "memory"
    );
    return a + b;
}

__attribute__((noinline))
int multiply(int a, int b) {
    int result = 0;
    for (int i = 0; i < b; i++) {
        result = add(result, a);
    }
    return result;
}

__attribute__((noinline))
long factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

typedef struct {
    int id;
    int a;
    int b;
} thread_arg_t;

void* worker_routine(void* arg) {
    thread_arg_t *args = (thread_arg_t*)arg;
    int id = args->id;
    int a = args->a;
    int b = args->b;
    free(args);

    pthread_mutex_lock(&print_lock);
    printf("[Thread %d] Started. Params: a=%d, b=%d\n", id, a, b);
    pthread_mutex_unlock(&print_lock);

    int sum = add(a, b);
    
    int prod = multiply(a, b);

    long fact = factorial(a);

    pthread_mutex_lock(&print_lock);
    printf("[Thread %d] Results -> Sum: %d, Prod: %d, Fact: %ld\n", id, sum, prod, fact);
    printf("[Thread %d] Exiting safely.\n", id);
    pthread_mutex_unlock(&print_lock);

    return NULL;
}

#define NUM_THREADS 4

int main(int argc, char *argv[]) {
    int a = 5;
    int b = 3;

    if (argc > 1) a = atoi(argv[1]);
    if (argc > 2) b = atoi(argv[2]);

    pthread_t threads[NUM_THREADS];
    
    printf("--- Shadow Stack Multi-Thread Test Begin ---\n");

    for (int i = 0; i < NUM_THREADS; i++) {
        thread_arg_t *args = malloc(sizeof(thread_arg_t));
        args->id = i;
        args->a = a + i;
        args->b = b;

        if (pthread_create(&threads[i], NULL, worker_routine, args) != 0) {
            perror("Failed to create thread");
            return 1;
        }
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("--- Shadow Stack Multi-Thread Test Success! ---\n");
    return 0;
}