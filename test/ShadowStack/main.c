#include <stdio.h>
#include <stdlib.h>

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

int main(int argc, char *argv[]) {
    int a = 5;
    int b = 3;

    if (argc > 1) a = atoi(argv[1]);
    if (argc > 2) b = atoi(argv[2]);

    printf("--- Shadow Stack Test Begin ---\n");
    
    printf("Testing Leaf Function (Add)...\n");
    int sum = add(a, b);
    printf("Result: %d\n", sum);

    printf("Testing Nested Call (Multiply)...\n");
    int prod = multiply(a, b);
    printf("Result: %d\n", prod);

    printf("Testing Recursion (Factorial)...\n");
    long fact = factorial(a);
    printf("Result: %ld\n", fact);

    printf("--- Shadow Stack Test Success! ---\n");
    return 0;
}