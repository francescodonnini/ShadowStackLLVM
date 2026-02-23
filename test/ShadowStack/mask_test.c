#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define ARRAY_SIZE 1024 * 1024 // 1 Million elements

/**
 * TEST: Heap Store Masking
 * These stores are targets for masking because 'data' is a heap pointer.
 */
void process_array(uint64_t *data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        // This store should be MASKED by your LLVM pass
        data[i] = i * 2; 
    }
}

/**
 * TEST: Multi-level pointer access
 * Complex pointer arithmetic is a great test for Alias Analysis.
 */
void matrix_fill(uint64_t **matrix, int rows, int cols) {
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            // These stores are definitely candidates for masking
            matrix[i][j] = (uint64_t)i + j;
        }
    }
}

int main() {
    printf("Starting Heap Stress Test...\n");

    // 1. Simple Array Test
    uint64_t *array = (uint64_t *)malloc(ARRAY_SIZE * sizeof(uint64_t));
    if (!array) return 1;

    printf("[1] Processing 1M elements on heap (Masking test)...\n");
    process_array(array, ARRAY_SIZE);

    // 2. Matrix Test (Pointer-to-Pointer)
    int rows = 100, cols = 100;
    uint64_t **matrix = (uint64_t **)malloc(rows * sizeof(uint64_t *));
    for (int i = 0; i < rows; i++) {
        matrix[i] = (uint64_t *)malloc(cols * sizeof(uint64_t));
    }

    printf("[2] Filling 100x100 matrix on heap...\n");
    matrix_fill(matrix, rows, cols);

    // 3. Verification of results
    printf("[3] Verifying data: array[500] = %lu, matrix[50][50] = %lu\n", 
           array[500], matrix[50][50]);

    // Cleanup
    for (int i = 0; i < rows; i++) free(matrix[i]);
    free(matrix);
    free(array);

    printf("Heap Stress Test completed successfully.\n");
    return 0;
}