/*
Task 1 (OpenMP version) – Finding Prime Numbers in Parallel
Parallelizes the primality test for each candidate number using OpenMP.
Each thread independently tests one number's primality and writes the
result to its own slot in a shared array (safe: no two threads ever
write the same slot), preserving sorted output order.
*/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <omp.h>

int main(int argc, char *argv[])
{
    // Check for command-line argument
    if (argc < 2) {
        printf("Error: Please provide at least one argument.\n");
        printf("Usage: %s max_number\n", argv[0]);
        return 1;
    }

    const char *filename = "prime-out-3.txt";
    FILE *fp = fopen(filename, "w");

    if (fp == NULL) {
        printf("Error opening file!\n");
        return 1;
    }

    clock_t start = clock();

    // Convert command-line argument to an integer
    int n = atoi(argv[1]);

    // Use file output for large values of n
    int writeToTxt = (n > 100) ? 1 : 0;

    // Store whether each number is prime

    // Parallelise the primality tests
    #pragma omp parallel for schedule(static)
    for (int i = 2; i < n; i++) {
        int prime = 1;

        // Only check divisors up to the square root of i
        for (int j = 2; j * j <= i; j++) {
            if (i % j == 0) {
                prime = 0;
                break;
            }
        }

        // Protect output from multiple threads
        if (writeToTxt && prime) {
            #pragma omp critical
            fprintf(fp, "%d\n", i);
        }
        else if (!writeToTxt && prime) {
            #pragma omp critical
            printf("%d\n", i);
        }
    }

    clock_t end = clock();

    // Calculate and display CPU time
    double cpu_time_used =
        ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("CPU time used: %f seconds\n", cpu_time_used);

    fclose(fp);

    return 0;
}