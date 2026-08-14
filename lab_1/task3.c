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

    const char *filename = "prime-out-openmp.txt";
    FILE *fp = fopen(filename, "w");

    if (fp == NULL) {
        printf("Error opening file!\n");
        return 1;
    }

    double start = omp_get_wtime();

    // Convert command-line argument to an integer
    int n = atoi(argv[1]);

    // Use file output for large values of n
    int writeToTxt = (n > 100) ? 1 : 0;

    // Store whether each number is prime
    int *is_prime = calloc(n, sizeof(int));
    if (is_prime == NULL) {
        printf("Error allocating memory!\n");
        fclose(fp);
        return 1;
    }

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

        is_prime[i] = prime;
    }

    // Print after all threads finish so the results stay in ascending order
    for (int i = 2; i < n; i++) {
        if (is_prime[i]) {
            if (writeToTxt) {
                fprintf(fp, "%d\n", i);
            } else {
                printf("%d\n", i);
            }
        }
    }

    double cpu_time_used = omp_get_wtime() - start;

    printf("CPU time used: %f seconds\n", cpu_time_used);

    free(is_prime);
    fclose(fp);

    return 0;
}
