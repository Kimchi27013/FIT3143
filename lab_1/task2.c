#define _POSIX_C_SOURCE 200809L

/*
Task 2 (POSIX threads version) - Finding Prime Numbers in Parallel

Worker threads dynamically claim chunks of odd candidates, test them, and
update a shared primality array. After every worker terminates, main joins
them and constructs the sorted output serially.
*/

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PRIME_CHUNK_SIZE 64
#define INITIAL_BUFFER_CAPACITY 4096

struct shared_work {
    int n;
    int next_candidate;
    int start_ready;
    int cancel;
    unsigned char *is_prime;
    pthread_mutex_t work_mutex;
    pthread_mutex_t start_mutex;
    pthread_cond_t start_condition;
};

struct thread_args {
    struct shared_work *work;
};

static int append_number(char **buffer, size_t *length, size_t *capacity,
                         int number)
{
    char text[32];
    int written = snprintf(text, sizeof(text), "%d\n", number);

    if (written < 0) {
        return -1;
    }

    size_t text_length = (size_t)written;
    if (text_length > SIZE_MAX - *length) {
        return -1;
    }

    size_t required = *length + text_length;
    if (required > *capacity) {
        size_t new_capacity = *capacity;

        while (new_capacity < required) {
            if (new_capacity > SIZE_MAX / 2) {
                new_capacity = required;
                break;
            }
            new_capacity *= 2;
        }

        char *resized = realloc(*buffer, new_capacity);
        if (resized == NULL) {
            return -1;
        }

        *buffer = resized;
        *capacity = new_capacity;
    }

    memcpy(*buffer + *length, text, text_length);
    *length = required;
    return 0;
}

static void *worker(void *argument)
{
    struct thread_args *thread = argument;
    struct shared_work *work = thread->work;

    /* Do not begin until main confirms that every worker was created. */
    pthread_mutex_lock(&work->start_mutex);
    while (!work->start_ready) {
        pthread_cond_wait(&work->start_condition, &work->start_mutex);
    }
    int cancel = work->cancel;
    pthread_mutex_unlock(&work->start_mutex);

    if (cancel) {
        return NULL;
    }

    /* Dynamic scheduling: claim 64 odd candidates at a time. */
    for (;;) {
        pthread_mutex_lock(&work->work_mutex);
        int first = work->next_candidate;
        work->next_candidate += 2 * PRIME_CHUNK_SIZE;
        pthread_mutex_unlock(&work->work_mutex);

        if (first >= work->n) {
            break;
        }

        int end = first + 2 * PRIME_CHUNK_SIZE;
        if (end > work->n) {
            end = work->n;
        }

        for (int candidate = first; candidate < end; candidate += 2) {
            int prime = 1;

            for (int divisor = 3; divisor <= candidate / divisor;
                 divisor += 2) {
                if (candidate % divisor == 0) {
                    prime = 0;
                    break;
                }
            }

            work->is_prime[candidate] = (unsigned char)prime;
        }
    }

    return NULL;
}

static double elapsed_seconds(const struct timespec *start,
                              const struct timespec *end)
{
    return (double)(end->tv_sec - start->tv_sec) +
           (end->tv_nsec - start->tv_nsec) / 1e9;
}

int main(int argc, char *argv[])
{
    // Check for command-line argument
    if (argc < 2) {
        printf("Error: Please provide at least one argument.\n");
        printf("Usage: %s max_number\n", argv[0]);
        return 1;
    }

    const char *filename = "prime-out-posix.txt";

    int nthreads = (int)sysconf(_SC_NPROCESSORS_ONLN);
    int n = atoi(argv[1]);
    int write_to_file = n > 100;
    struct timespec start_time;
    struct timespec end_time;

    if (clock_gettime(CLOCK_MONOTONIC, &start_time) != 0) {
        perror("Failed to start the timer");
        return EXIT_FAILURE;
    }

    unsigned char *is_prime = calloc((size_t)n, sizeof(*is_prime));
    pthread_t *threads = malloc((size_t)nthreads * sizeof(*threads));
    struct thread_args *arguments =
        calloc((size_t)nthreads, sizeof(*arguments));

    if (is_prime == NULL || threads == NULL || arguments == NULL) {
        fprintf(stderr, "Error allocating benchmark data.\n");
        free(is_prime);
        free(threads);
        free(arguments);
        return EXIT_FAILURE;
    }

    if (n > 2) {
        is_prime[2] = 1;
    }

    struct shared_work work = {
        .n = n,
        .next_candidate = 3,
        .start_ready = 0,
        .cancel = 0,
        .is_prime = is_prime
    };

    if (pthread_mutex_init(&work.work_mutex, NULL) != 0 ||
        pthread_mutex_init(&work.start_mutex, NULL) != 0 ||
        pthread_cond_init(&work.start_condition, NULL) != 0) {
        fprintf(stderr, "Error initialising POSIX synchronisation.\n");
        free(is_prime);
        free(threads);
        free(arguments);
        return EXIT_FAILURE;
    }

    int created_threads = 0;
    for (int thread = 0; thread < nthreads; thread++) {
        arguments[thread].work = &work;

        int error = pthread_create(&threads[thread], NULL, worker,
                                    &arguments[thread]);
        if (error != 0) {
            fprintf(stderr,
                    "Failed to create thread %d (error %d).\n",
                    thread, error);
            break;
        }
        created_threads++;
    }

    pthread_mutex_lock(&work.start_mutex);
    work.cancel = created_threads != nthreads;
    work.start_ready = 1;
    pthread_cond_broadcast(&work.start_condition);
    pthread_mutex_unlock(&work.start_mutex);

    int join_error = 0;
    for (int thread = 0; thread < created_threads; thread++) {
        int error = pthread_join(threads[thread], NULL);
        if (error != 0) {
            fprintf(stderr, "Failed to join thread %d (error %d).\n",
                    thread, error);
            join_error = 1;
        }
    }

    pthread_cond_destroy(&work.start_condition);
    pthread_mutex_destroy(&work.start_mutex);
    pthread_mutex_destroy(&work.work_mutex);

    if (created_threads != nthreads || join_error) {
        free(is_prime);
        free(threads);
        free(arguments);
        return EXIT_FAILURE;
    }

    /* Joining guarantees that every is_prime update is complete. */
    size_t output_capacity = INITIAL_BUFFER_CAPACITY;
    size_t total_length = 0;
    char *output_buffer = malloc(output_capacity);
    if (output_buffer == NULL) {
        fprintf(stderr, "Error allocating the output buffer.\n");
        free(is_prime);
        free(threads);
        free(arguments);
        return EXIT_FAILURE;
    }

    for (int candidate = 2; candidate < n; candidate++) {
        if (is_prime[candidate] &&
            append_number(&output_buffer, &total_length,
                            &output_capacity, candidate) != 0) {
            fprintf(stderr, "Error constructing the output buffer.\n");
            free(output_buffer);
            free(is_prime);
            free(threads);
            free(arguments);
            return EXIT_FAILURE;
        }
    }

    if (write_to_file) {
        FILE *output = fopen(filename, "w");
        if (output == NULL) {
            /* Retry transient failures from cloud-backed drives. */
            for (int attempt = 0; attempt < 5 && output == NULL;
                    attempt++) {
                output = fopen(filename, "w");
            }
            if (output == NULL) {
                perror("Error opening output file");
                free(output_buffer);
                free(is_prime);
                free(threads);
                free(arguments);
                return EXIT_FAILURE;
            }
        }

        size_t bytes_written =
            fwrite(output_buffer, 1, total_length, output);
        if (bytes_written != total_length) {
            fprintf(stderr, "Error writing the output file.\n");
            fclose(output);
            free(output_buffer);
            free(is_prime);
            free(threads);
            free(arguments);
            return EXIT_FAILURE;
        }
        fclose(output);
    } else {
        fwrite(output_buffer, 1, total_length, stdout);
    }

    if (clock_gettime(CLOCK_MONOTONIC, &end_time) != 0) {
        perror("Failed to stop the timer");
        free(output_buffer);
        free(is_prime);
        free(threads);
        free(arguments);
        return EXIT_FAILURE;
    }

    double cpu_time_used = elapsed_seconds(&start_time, &end_time);
    printf("CPU time used: %f seconds\n", cpu_time_used);

    free(output_buffer);
    free(is_prime);
    free(threads);
    free(arguments);

    return EXIT_SUCCESS;
}
