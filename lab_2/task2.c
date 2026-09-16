/*
Task 2 (Hybrid MPI + OpenMP version) - Finding Prime Numbers in Parallel
Combines distributed-memory parallelism (MPI, across processes/nodes) with
shared-memory parallelism (OpenMP, across threads within a process).

Compile:  mpicc -O2 -fopenmp -o task2 task2.c -lm
Run:      mpirun -np 1 ./task2 100000000 1
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <mpi.h>
#include <omp.h>

/* ---- fast integer-to-string (replaces extremely slow snprintf) ---- */
// AI Declaration - This was written by AI
static inline int fast_itoa(int val, char* buf) {
    char temp[12];
    int len = 0;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\n';
        return 2;
    }
    while (val > 0) {
        temp[len++] = (char)('0' + (val % 10));
        val /= 10;
    }
    for (int i = 0; i < len; i++) {
        buf[i] = temp[len - 1 - i];
    }
    buf[len] = '\n';
    return len + 1;
}

/* ---- small-prime sieve, used to speed up trial division ---- */
// AI Declaration - This was written by AI
static int *sieve_small_primes(int limit, int *count_out)
{
    if (limit < 2) { *count_out = 0; return NULL; }

    char *composite = calloc((size_t)limit + 1, sizeof(char));
    int *primes = malloc(((size_t)limit / 2 + 2) * sizeof(int));
    int count = 0;

    for (int i = 2; i <= limit; i++) {
        if (!composite[i]) {
            primes[count++] = i;
            for (long j = (long)i * i; j <= limit; j += i) composite[j] = 1;
        }
    }
    free(composite);
    *count_out = count;
    return primes;
}

/* ---- equal-WORK boundary table: x_k = n * (k/P)^(2/3) ---- */
static void compute_boundaries(int n, int nprocs, int *boundaries)
{
    boundaries[0] = 2; /* nothing useful below 2 */
    for (int k = 1; k < nprocs; k++) {
        double frac = (double)k / (double)nprocs;
        int x = (int)(pow(frac, 2.0 / 3.0) * (double)n);
        if (x < boundaries[k - 1]) x = boundaries[k - 1]; /* monotonic guard */
        boundaries[k] = x;
    }
    boundaries[nprocs] = n;
}

int main(int argc, char *argv[])
{
    int rank, nprocs;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    /* ==========================================================
       PHASE 1: SERIAL INITIALIZATION (Setup & Dissemination)
       ========================================================== */
    double t_init_start = MPI_Wtime();

    int n = 0;
    int requested_threads = 0; 

    if (rank == 0) {
        if (argc < 2) {
            fprintf(stderr, "Usage: %s max_number [num_threads_per_process]\n", argv[0]);
            n = -1; 
        } else {
            n = atoi(argv[1]);
            if (argc >= 3) requested_threads = atoi(argv[2]);
        }
    }

    MPI_Bcast(&n, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&requested_threads, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (n < 2) {
        if (rank == 0) fprintf(stderr, "Error: n must be >= 2.\n");
        MPI_Finalize();
        return 1;
    }

    if (requested_threads > 0) omp_set_num_threads(requested_threads);
    int nthreads = omp_get_max_threads();

    double t_init_end = MPI_Wtime();

    /* ==========================================================
       PHASE 2: PARALLEL COMPUTATION (MPI Domain + OpenMP Threads)
       ========================================================== */
    double t_compute_start = MPI_Wtime();

    int *boundaries = malloc((nprocs + 1) * sizeof(int));
    compute_boundaries(n, nprocs, boundaries);
    int lo = boundaries[rank];
    int hi = boundaries[rank + 1]; 
    free(boundaries);

    int local_n = hi - lo;
    if (local_n < 0) local_n = 0;

    int sqrt_n = (int)sqrt((double)n) + 1;
    int small_count = 0;
    int *small_primes = sieve_small_primes(sqrt_n, &small_count);

    char *is_prime = calloc((size_t)(local_n > 0 ? local_n : 1), sizeof(char));

    if (lo <= 2 && 2 < hi) is_prime[2 - lo] = 1;

    int odd_start = (lo % 2 == 0) ? lo + 1 : lo;
    if (odd_start < 3) odd_start = 3;

    #pragma omp parallel for schedule(dynamic, 64)
    for (int i = odd_start; i < hi; i += 2) {
        int prime = 1;
        int limit = (int)sqrt((double)i);
        
        for (int p = 1; p < small_count; p++) {
            int d = small_primes[p];
            if (d > limit) break; 
            if (i % d == 0) { prime = 0; break; }
        }
        is_prime[i - lo] = (char)prime;
    }

    char **local_bufs = malloc((size_t)nthreads * sizeof(char *));
    size_t *local_lens = calloc((size_t)nthreads, sizeof(size_t));

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        size_t max_items = (size_t)(local_n / nthreads) + 2; 
        size_t cap = max_items * 12;
        char *lbuf = malloc(cap);
        size_t llen = 0;

        if (lbuf != NULL) {
            #pragma omp for schedule(static)
            for (int idx = 0; idx < local_n; idx++) {
                if (is_prime[idx]) {
                    llen += fast_itoa(lo + idx, lbuf + llen);
                }
            }
        }
        local_bufs[tid] = lbuf;
        local_lens[tid] = llen;
    }

    size_t proc_len = 0;
    for (int t = 0; t < nthreads; t++) proc_len += local_lens[t];

    char *proc_buf = malloc(proc_len > 0 ? proc_len : 1);
    size_t off = 0;
    for (int t = 0; t < nthreads; t++) {
        if (local_bufs[t] != NULL) {
            memcpy(proc_buf + off, local_bufs[t], local_lens[t]);
            off += local_lens[t];
        }
        free(local_bufs[t]);
    }
    free(local_bufs);
    free(local_lens);
    free(is_prime);
    free(small_primes);

    double t_compute_end = MPI_Wtime();

    /* ==========================================================
       PHASE 3: SERIAL COMMUNICATION & I/O (Gather and Write)
       ========================================================== */
    double t_comm_start = MPI_Wtime();

    int send_count = (int)off; 
    int *recv_counts = NULL, *displs = NULL;
    char *final_buf = NULL;
    int total_len = 0;

    if (rank == 0) recv_counts = malloc((size_t)nprocs * sizeof(int));
    MPI_Gather(&send_count, 1, MPI_INT, recv_counts, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        displs = malloc((size_t)nprocs * sizeof(int));
        displs[0] = 0;
        for (int r = 1; r < nprocs; r++) displs[r] = displs[r - 1] + recv_counts[r - 1];
        total_len = displs[nprocs - 1] + recv_counts[nprocs - 1];
        final_buf = malloc(total_len > 0 ? (size_t)total_len : 1);
    }

    MPI_Gatherv(proc_buf, send_count, MPI_CHAR,
                final_buf, recv_counts, displs, MPI_CHAR,
                0, MPI_COMM_WORLD);
    free(proc_buf);

    if (rank == 0) {
        const char *filename = "prime-out-hybrid.txt";
        FILE *fp = fopen(filename, "w");
        if (fp != NULL) {
            fwrite(final_buf, 1, (size_t)total_len, fp);
            fclose(fp);
        }
        free(final_buf);
        free(recv_counts);
        free(displs);
    }

    double t_comm_end = MPI_Wtime();

    /* ==========================================================
       TIMING REDUCTION AND ANALYSIS OUTPUT
       ========================================================== */
    double local_init = t_init_end - t_init_start;
    double local_compute = t_compute_end - t_compute_start;
    double local_comm = t_comm_end - t_comm_start;

    double max_init, max_compute, max_comm;

    MPI_Reduce(&local_init, &max_init, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_compute, &max_compute, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_comm, &max_comm, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        double total_serial = max_init + max_comm;
        double total_time = total_serial + max_compute;
        
        printf("\n--- Profiling Results (np=%d, threads=%d, n=%d) ---\n", nprocs, nthreads, n);
        printf("T_serial_init : %f s\n", max_init);
        printf("T_parallel    : %f s\n", max_compute);
        printf("T_serial_comm : %f s\n", max_comm);
        printf("----------------------------------------------------\n");
        printf("Total Serial (T_s)  : %f s\n", total_serial);
        printf("Total Parallel (T_p): %f s\n", max_compute);
        printf("Total Execution     : %f s\n", total_time);
        
        // Exact fractions for Amdahl's and Gustafson's Laws
        printf("Serial Fraction (1-f) : %f\n", total_serial / total_time);
        printf("Parallel Fraction (f) : %f\n", max_compute / total_time);

        FILE *log = fopen("timing_log_hybrid_detailed.csv", "a");
        if (log != NULL) {
            fprintf(log, "%d,%d,%d,%f,%f,%f,%f,%f\n",
                    n, nprocs, nthreads, max_init, max_compute, max_comm, total_serial, total_time);
            fclose(log);
        }
    }

    MPI_Finalize();
    return 0;
}