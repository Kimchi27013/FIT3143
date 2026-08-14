/*
Write and implement a parallel version (“task2.c”) of your serial code in C utilising POSIX
Threads.
In this part, your team will need to design a parallel partitioning scheme to distribute the workload
among the threads and implement it in C.
After you implement the task, measure again the time required to search for prime numbers less
than an integer n and compute the speed-up by your parallel implementation. Make sure your
code can still produce a sorted list of prime numbers (in ascending order). You should test for
different values of n and tabulate the serial and parallel computation times. Based on the
tabulated results, you can compute the speedup for different n values. We recommend that you
first test with n > 10,000,000 and then increase n.

*/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

#define NUM_THREADS 12

struct thread_args {
    int length;
    int* range;
    int* prime_list;
};

void* calculate_prime(void *arg) {
    struct thread_args *data = (struct thread_args *)arg;

    int length = data->length;
    int* range = data->range;
    int* prime_list = data->prime_list;

    int prime;
    int p;

    for (int i = 0; i<length; i++) {
        p = range[i];
        prime = 1;
        for (int j = 2; j*j < p; j++) {
            if (p%j == 0) {
                prime = 0;
                break;
            }
        }

        prime_list[p-1] = prime;
    }

    free(data);

    pthread_exit(NULL); // Terminate thread cleanly
}

int main(int argc, char *argv[])
{	
    if (argc < 2) {
        printf("Error: Please provide at least one argument.\n");
        printf("Usage: %s max_number\n", argv[0]);
        return 1; // Return non-zero to indicate an error
    }
    
    const char *filename = "prime-out-posix.txt";

    FILE *fp = fopen(filename, "w");
    
    if (fp == NULL) {
        printf("Error opening file!\n");
        return 1;
    }

    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);

    pthread_t threads[NUM_THREADS];
    struct thread_args thread_args[NUM_THREADS];

    int *prime_indicator = malloc((atoi(argv[1])) * sizeof(int));

    for (int i=0; i < NUM_THREADS; i++) {
        struct thread_args *args = malloc(sizeof(struct thread_args));
        if (args == NULL) {
            perror("Failed to allocate memory");
            return 1;
        }

        int *list = malloc(((atoi(argv[1])-1)/2)*sizeof(int));
        for (int j=i*atoi(argv[1])/NUM_THREADS+2; j<i*atoi(argv[1])/NUM_THREADS+2+atoi(argv[1])/NUM_THREADS; j++) {
            list1[j-(i*atoi(argv[1])/NUM_THREADS+2)] = j;
        }
    }

    }


    

    // 4. Initialize the data structure
    args1->length = ((atoi(argv[1])-1)/2);
    args1->range = list1;
    args1->prime_list = prime_indicator;

    if (atoi(argv[1])%2 == 0) {
        args2->length = ((atoi(argv[1])-1)/2);
    }
    else {
        args2->length = ((atoi(argv[1])-1)/2) - 1;
    }
    args2->range = list2;
    args2->prime_list = prime_indicator;
    
    // 1. Create threads
    // Args: (thread_id, attributes, routine_function, function_arguments)
    if (pthread_create(&thread1, NULL, calculate_prime, (void*) args1) != 0) {
        perror("Failed to create thread 1");
        free(args1);
        return 1;
    }
    
    if (pthread_create(&thread2, NULL, calculate_prime, (void*) args2) != 0) {
        perror("Failed to create thread 2");
        free(args2);
        return 1;
    }

    // 2. Wait for threads to finish execution
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);

    for (int i=2; i<atoi(argv[1]); i++) {
        if (prime_indicator[i-1] == 1) {
            if (atoi(argv[1]) <= 100) {
                printf("%d\n", i);
            } 
            else {
                fprintf(fp, "%d\n", i);
            }
        }
    }


    clock_gettime(CLOCK_MONOTONIC, &end);

    double cpu_time_used = (end.tv_sec - start.tv_sec) +
                 (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("CPU time used: %f seconds\n", cpu_time_used);

    free(list1);
    free(list2);
    // free(args1);
    // free(args2);
    free(prime_indicator);

    return 0;
}