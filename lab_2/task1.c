/*
Task 1 (parallel) - Finding Prime Numbers with MPI (Instrumented)

Compile:  mpicc -O2 -o task1 task1.c
Run:      mpirun -np 4 ./task1 100000000
*/

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

static inline int is_prime(int num)
{
    if (num < 2) return 0;
    if (num == 2 || num == 3) return 1;
    if (num % 2 == 0 || num % 3 == 0) return 0;

    for (int j = 5; (long long)j * j <= num; j += 6) {
        if (num % j == 0 || num % (j + 2) == 0) return 0;
    }
    return 1;
}

static int cmp_int(const void *a, const void *b)
{
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}

#define BLOCKS_PER_RANK 32

int main(int argc, char *argv[])
{
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    /* SERIAL INITIALIZATION */
    double t_init_start = MPI_Wtime();
    
    int n = 0;
    if (rank == 0) {
        if (argc < 2) {
            printf("Error: Please provide at least one argument.\n");
            printf("Usage: mpirun -np <procs> %s max_number\n", argv[0]);
            n = -1; // Error flag
        } else {
            n = atoi(argv[1]);
        }
    }
    
    // Broadcast n to all processes (creates measurable communication overhead)
    MPI_Bcast(&n, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (n < 2) {
        MPI_Finalize();
        return 1;
    }
    
    double t_init_end = MPI_Wtime();

    /* PARALLEL COMPONENT */
    double t_compute_start = MPI_Wtime();

    int range = (n > 2) ? (n - 2) : 0;
    int block_size = range / (size * BLOCKS_PER_RANK);
    if (block_size < 1) block_size = 1;
    int num_blocks = (range + block_size - 1) / block_size;

    int local_capacity = 1024;
    int *local_primes = malloc(local_capacity * sizeof(int));
    int local_prime_count = 0;

    for (int b = rank; b < num_blocks; b += size) {
        int block_start = 2 + b * block_size;
        int block_end = block_start + block_size;
        if (block_end > n) block_end = n;

        for (int i = block_start; i < block_end; i++) {
            if (is_prime(i)) {
                if (local_prime_count == local_capacity) {
                    local_capacity *= 2;
                    local_primes = realloc(local_primes, local_capacity * sizeof(int));
                }
                local_primes[local_prime_count++] = i;
            }
        }
    }

    double t_compute_end = MPI_Wtime();

    /* SERIAL GATHER, SORT AND WRITE */
    double t_comm_start = MPI_Wtime();

    int *recv_counts = NULL;
    int *displs = NULL;
    if (rank == 0) {
        recv_counts = malloc(size * sizeof(int));
        displs = malloc(size * sizeof(int));
    }

    MPI_Gather(&local_prime_count, 1, MPI_INT, recv_counts, 1, MPI_INT, 0, MPI_COMM_WORLD);

    int total_primes = 0;
    int *all_primes = NULL;
    if (rank == 0) {
        displs[0] = 0;
        for (int i = 1; i < size; i++) {
            displs[i] = displs[i - 1] + recv_counts[i - 1];
        }
        total_primes = displs[size - 1] + recv_counts[size - 1];
        all_primes = malloc((total_primes > 0 ? total_primes : 1) * sizeof(int));
    }

    MPI_Gatherv(local_primes, local_prime_count, MPI_INT,
                all_primes, recv_counts, displs, MPI_INT,
                0, MPI_COMM_WORLD);

    if (rank == 0 && total_primes > 1) {
        qsort(all_primes, total_primes, sizeof(int), cmp_int);
    }

    if (rank == 0) {
        int write_to_file = (n > 100) ? 1 : 0;
        if (write_to_file) {
            const char *filename = "prime-openmpi.txt";
            FILE *fp = fopen(filename, "w");
            if (fp == NULL) {
                printf("Error opening file!\n");
            } else {
                for (int i = 0; i < total_primes; i++) {
                    fprintf(fp, "%d\n", all_primes[i]);
                }
                fclose(fp);
            }
        }
        free(all_primes);
        free(recv_counts);
        free(displs);
    }
    free(local_primes);

    double t_comm_end = MPI_Wtime();

    /*  CODE FOR THE TIMING ANALYSIS
        NOTE: This part was written using AI
    */
    double local_init_time = t_init_end - t_init_start;
    double local_compute_time = t_compute_end - t_compute_start;
    double local_comm_time = t_comm_end - t_comm_start;

    double max_init, max_compute, max_comm;
    
    // We use MPI_MAX to find the slowest process in each phase,
    // which represents the true "wall-clock" limit.
    MPI_Reduce(&local_init_time, &max_init, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_compute_time, &max_compute, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_comm_time, &max_comm, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        double total_serial = max_init + max_comm;
        double total_time = total_serial + max_compute;
        
        printf("\n--- Profiling Results (np=%d, n=%d) ---\n", size, n);
        printf("T_serial_init : %f s\n", max_init);
        printf("T_parallel    : %f s\n", max_compute);
        printf("T_serial_comm : %f s\n", max_comm);
        printf("--------------------------------------\n");
        printf("Total Serial (T_s)  : %f s\n", total_serial);
        printf("Total Parallel (T_p): %f s\n", max_compute);
        printf("Total Execution     : %f s\n", total_time);
        
        // These are the exact fractions you need for Amdahl's/Gustafson's Laws!
        printf("Serial Fraction (f) : %f\n", total_serial / total_time);
        printf("Parallel Fraction   : %f\n", max_compute / total_time);
    }

    MPI_Finalize();
    return 0;
}