/*
Task 3 (OpenMP version) – Finding Prime Numbers in Parallel
Parallelizes both the primality test AND the output-buffer construction
to minimize serial (non-parallelizable) overhead, improving speedup
scaling per Amdahl's Law.

Improvements over baseline:
  - dynamic scheduling to fix load imbalance 
  - output buffer built in PARALLEL: each thread fills its own local buffer,
    then buffers are concatenated with fast memcpy (no serial string loop)
  - skips even numbers and only checks up to sqrt(i)
  - single fwrite instead of per-line fprintf/printf
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Error: Please provide at least one argument.\n");
        printf("Usage: %s max_number\n", argv[0]);
        return 1;
    }

    int n = atoi(argv[1]);
    if (n < 2) { 
        printf("No primes below %d.\n", n);
        return 0;
    }

    // Files for testing purposes
    int writeToTxt = (n > 100) ? 1 : 0;
    const char *filename = "prime-out-openmp.txt";
    const char *logfile = "timing_log.csv";

    double start = omp_get_wtime();

    // is_prime[i] = 1 if i is prime, 0 otherwise.
    // Using char instead of int: 1 byte per entry instead of 4,
    char *is_prime = calloc(n, sizeof(char));
    if (is_prime == NULL) {
        printf("Error allocating memory!\n");
        return 1;
    }

    if (n > 2) is_prime[2] = 1;

    // Key Improvement 1 - Dyanmic Scheduling
    // schedule(dynamic, 64) fixes load imbalance from using static scheduler
    #pragma omp parallel for schedule(dynamic, 64)
    for (int i = 3; i < n; i += 2) { // Key Improvement 2 - Only checking even values greater than 2
        int prime = 1;
        // only check values up to sqrt(i)
        for (int j = 3; (long)j * j <= i; j += 2) { // Key Improvement 2 - Cast j to long to increase possible value of integer; needed for larger values of n
            if (i % j == 0) {
                prime = 0;
                break;
            }
        }
        is_prime[i] = (char)prime;
    }

    // Key Improvement 3 - Thread-Local Buffers
    // After checking if number is prime, thread stores prime number into its own local buffer, which is then combined into a central buffer to print
    int nthreads = omp_get_max_threads();
    char **local_bufs = malloc(nthreads * sizeof(char *));
    size_t *local_lens = malloc(nthreads * sizeof(size_t));

    // Checking for allocation errors
    if (local_bufs == NULL || local_lens == NULL) {
        printf("Error allocating thread buffer arrays!\n");
        free(is_prime);
        free(local_bufs);
        free(local_lens);
        return 1;
    }

    #pragma omp parallel
    {
        int threadId = omp_get_thread_num();

        // Estimates the maximum memory dynamically (based on input n value), then splits the memory across each buffer
        size_t local_cap = ((size_t)n * 8) / nthreads + 4096; //  Extra 4096 bytes just in case
        char *lbuf = malloc(local_cap);
        size_t llen = 0;

        if (lbuf != NULL) {
            // Use static scheduler here as the overhead cost of storing values in buffer is always the same
            #pragma omp for schedule(static)
            for (int i = 2; i < n; i++) {
                if (is_prime[i]) {
                    llen += snprintf(lbuf + llen, local_cap - llen, "%d\n", i);
                }
            }
        }

        local_bufs[threadId] = lbuf;
        local_lens[threadId] = llen;
    }

    // Combines each individual thread buffer into final output buffer (buf)
    size_t total_len = 0;
    for (int t = 0; t < nthreads; t++) total_len += local_lens[t];

    // Allocating final buffer size dynamically
    char *buf = malloc(total_len > 0 ? total_len : 1);
    if (buf == NULL) {
        printf("Error allocating final output buffer!\n");
        for (int t = 0; t < nthreads; t++) free(local_bufs[t]);
        free(local_bufs);
        free(local_lens);
        free(is_prime);
        return 1;
    }

    // Combines each thread's buffer into output buffer using memcpy
    size_t offset = 0;
    for (int t = 0; t < nthreads; t++) {
        if (local_bufs[t] != NULL) {
            memcpy(buf + offset, local_bufs[t], local_lens[t]); // No need to .sort() as static scheduling preserves order of prime numbers
            offset += local_lens[t];
        }
        free(local_bufs[t]);
    }
    free(local_bufs);
    free(local_lens);

    // Key Improvement 4 - Single fwrite
    // Output everything via a single fwrite operation instead of repeated printf calls 
    if (writeToTxt) {
        FILE *fp = fopen(filename, "w");
        if (fp == NULL) {
            printf("Error opening file!\n");
            free(is_prime);
            free(buf);
            return 1;
        }
        fwrite(buf, 1, offset, fp);
        fclose(fp);
    } else {
        fwrite(buf, 1, offset, stdout);
    }

    double cpu_time_used = omp_get_wtime() - start;
    printf("CPU time used: %f seconds\n", cpu_time_used);

    // Log results for speedup analysis: n, threads used, time taken.
    FILE *log = fopen(logfile, "a");
    if (log != NULL) {
        fprintf(log, "%d,%d,%f\n", n, nthreads, cpu_time_used);
        fclose(log);
    } else {
        printf("Warning: could not open %s for logging.\n", logfile);
    }

    free(is_prime);
    free(buf);

    return 0;
}