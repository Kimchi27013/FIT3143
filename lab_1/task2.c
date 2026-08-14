/*
Parallel prime-number search using POSIX threads.

The program finds all prime numbers strictly less than the positive integer
given on the command line. Results are printed to standard output when
n <= 100 and written to prime-out-posix.txt otherwise.
*/

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NUM_THREADS 12

struct thread_args {
    int start;
    int end;
    unsigned char *prime_list;
};

static void *calculate_prime(void *arg)
{
    struct thread_args *data = arg;

    for (int p = data->start; p < data->end; p++) {
        int prime = 1;

        /* Using p / j avoids overflow from calculating j * j. */
        for (int j = 2; j <= p / j; j++) {
            if (p % j == 0) {
                prime = 0;
                break;
            }
        }

        data->prime_list[p] = (unsigned char)prime;
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s max_number\n", argv[0]);
        return EXIT_FAILURE;
    }

    errno = 0;
    char *end_pointer = NULL;
    long input = strtol(argv[1], &end_pointer, 10);

    if (errno != 0 || end_pointer == argv[1] || *end_pointer != '\0' ||
        input < 2 || input > INT_MAX) {
        fprintf(stderr, "Error: max_number must be an integer from 2 to %d.\n",
                INT_MAX);
        return EXIT_FAILURE;
    }

    int number = (int)input;
    const char *filename = "prime-out-posix.txt";
    FILE *fp = fopen(filename, "w");

    if (fp == NULL) {
        perror("Error opening output file");
        return EXIT_FAILURE;
    }

    unsigned char *prime_indicator = calloc((size_t)number,
                                             sizeof(*prime_indicator));
    if (prime_indicator == NULL) {
        perror("Failed to allocate the prime array");
        fclose(fp);
        return EXIT_FAILURE;
    }

    struct timespec start_time;
    struct timespec end_time;

    if (clock_gettime(CLOCK_MONOTONIC, &start_time) != 0) {
        perror("Failed to start the timer");
        free(prime_indicator);
        fclose(fp);
        return EXIT_FAILURE;
    }

    int candidate_count = number - 2;
    int thread_count = candidate_count < NUM_THREADS
                           ? candidate_count
                           : NUM_THREADS;
    pthread_t threads[NUM_THREADS];
    struct thread_args arguments[NUM_THREADS];
    int created_threads = 0;

    for (int i = 0; i < thread_count; i++) {
        /* These boundaries cover [2, number) exactly once. */
        arguments[i].start =
            2 + (int)((long long)i * candidate_count / thread_count);
        arguments[i].end =
            2 + (int)((long long)(i + 1) * candidate_count / thread_count);
        arguments[i].prime_list = prime_indicator;

        int error = pthread_create(&threads[i], NULL, calculate_prime,
                                   &arguments[i]);
        if (error != 0) {
            fprintf(stderr, "Failed to create thread %d (error %d).\n", i,
                    error);
            break;
        }

        created_threads++;
    }

    for (int i = 0; i < created_threads; i++) {
        int error = pthread_join(threads[i], NULL);
        if (error != 0) {
            fprintf(stderr, "Failed to join thread %d (error %d).\n", i,
                    error);
            free(prime_indicator);
            fclose(fp);
            return EXIT_FAILURE;
        }
    }

    if (created_threads != thread_count) {
        free(prime_indicator);
        fclose(fp);
        return EXIT_FAILURE;
    }

    for (int i = 2; i < number; i++) {
        if (prime_indicator[i]) {
            if (number <= 100) {
                printf("%d\n", i);
            } else {
                fprintf(fp, "%d\n", i);
            }
        }
    }

    if (clock_gettime(CLOCK_MONOTONIC, &end_time) != 0) {
        perror("Failed to stop the timer");
        free(prime_indicator);
        fclose(fp);
        return EXIT_FAILURE;
    }

    double cpu_time_used = (double)(end_time.tv_sec - start_time.tv_sec) +
                          (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    printf("CPU time used: %f seconds\n", cpu_time_used);

    free(prime_indicator);
    fclose(fp);
    return EXIT_SUCCESS;
}
