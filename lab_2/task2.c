/*
Task 4 (Hybrid MPI + OpenMP version) - Finding Prime Numbers in Parallel
Combines distributed-memory parallelism (MPI, across processes/nodes) with
shared-memory parallelism (OpenMP, across threads within a process).

===========================================================================
DESIGN NOTES (read this before the report write-up)
===========================================================================
1. Value dissemination
   Only rank 0 reads argv. n (and optionally the requested OpenMP thread
   count) is then MPI_Bcast to every other process, satisfying the
   requirement that the root reads the input and distributes it.

2. Workload distribution ACROSS processes (MPI level)
   Trial division tests i by dividing by every prime up to sqrt(i), so the
   cost of testing a single number i is ~O(sqrt(i)). A naive equal-COUNT
   split of [2, n) across processes therefore gives the process holding the
   highest numbers far more work than the process holding the lowest
   numbers (load imbalance grows with n).

   Instead this program splits the domain using an equal-WORK partition.
   Modelling cost(i) ~ sqrt(i), the cumulative cost up to x is
       C(x) = integral_2^x sqrt(t) dt  ~  (2/3) x^(3/2)
   so the boundary that gives process k the k-th 1/P share of the total
   work is
       x_k = n * (k / P)^(2/3),   k = 0 .. P
   Every rank computes this same boundary table locally (cheap - it only
   needs n and P), so no extra communication is required and each rank
   ends up with a *contiguous* range [start_k, end_k). Because ranges are
   contiguous and increasing with rank, gathering each process's sorted
   output in rank order reproduces the fully sorted list - no merge step
   needed on the root.

3. Workload distribution WITHIN a process (OpenMP level)
   Inside each process's contiguous range, #pragma omp parallel for with
   dynamic scheduling divides the range across threads. Dynamic scheduling
   is kept from the single-node version because even after the sqrt-weighted
   MPI split there is still fine-grained variance between individual
   numbers (e.g. an odd i that is divisible by 3 finishes almost instantly,
   a large prime needs the full sqrt(i) sweep), and dynamic scheduling
   absorbs that at the thread level far more effectively than static
   chunks.

4. Faster primality test
   Instead of trial-dividing by every odd number up to sqrt(i) (as in the
   single-node version), every rank first sieves the small primes up to
   sqrt(n) once (this is O(sqrt(n) log log sqrt(n)) work and is cheap and
   fully redundant/deterministic across ranks, so it needs no
   communication). Trial division then only tests against that short list
   of *primes*, which is a large constant-factor speedup over testing
   every odd number, especially for large n.

5. Output collection
   Each process builds its slice of "prime\n" text into thread-local
   buffers (same pattern as the single-node version: one buffer per
   OpenMP thread, combined with memcpy, avoiding lock contention and
   repeated I/O calls), producing one contiguous, sorted byte buffer per
   process. MPI_Gatherv then gathers the (variable-length) buffers from
   every process into the root in rank order. Because MPI ranks already
   hold contiguous, increasing sub-ranges, concatenation in rank order is
   already fully sorted - the root does a single fwrite() and is done.

6. Timing
   Each rank times its own compute (sieve + primality test + buffer build)
   phase and its Gatherv phase separately with MPI_Wtime. MPI_Reduce with
   MPI_MAX pulls out the slowest rank for each phase (the true
   "wall-clock" cost of that phase, since a parallel phase only finishes
   when its slowest participant finishes), and rank 0 logs
   n,nprocs,nthreads,compute_time,comm_time,total_time to a CSV for the
   speedup/efficiency analysis in the report.

AI Declaration:
  - AI was used to generate ideas for the equal-work MPI partitioning
    scheme and to help debug the MPI_Gatherv offset/count logic.

Compile:  mpicc -O2 -fopenmp -o task2 task2.c -lm
Run:      mpirun -np 4 ./task2 1000000
===========================================================================
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <mpi.h>
#include <omp.h>

/* ---- small-prime sieve, used to speed up trial division ---- */
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

    int n = 0;
    int requested_threads = 0; /* 0 => let OpenMP/env decide */

    /* ---- Root reads command-line arguments ---- */
    if (rank == 0) {
        if (argc < 2) {
            fprintf(stderr, "Usage: %s max_number [num_threads_per_process]\n", argv[0]);
            n = -1; /* signal error to broadcast */
        } else {
            n = atoi(argv[1]);
            if (argc >= 3) requested_threads = atoi(argv[2]);
        }
    }

    /* ---- Disseminate n (and thread count) to every process ---- */
    MPI_Bcast(&n, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&requested_threads, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (n < 2) {
        if (rank == 0) fprintf(stderr, "Error: n must be >= 2.\n");
        MPI_Finalize();
        return 1;
    }

    if (requested_threads > 0) omp_set_num_threads(requested_threads);
    int nthreads = omp_get_max_threads();

    double t_start = MPI_Wtime();

    /* ---- Each process figures out its own contiguous, equal-WORK range ---- */
    int *boundaries = malloc((nprocs + 1) * sizeof(int));
    compute_boundaries(n, nprocs, boundaries);
    int lo = boundaries[rank];
    int hi = boundaries[rank + 1]; /* exclusive */
    free(boundaries);

    int local_n = hi - lo;
    if (local_n < 0) local_n = 0;

    /* ---- Small-prime sieve up to sqrt(n), redundant but cheap on every rank ---- */
    int sqrt_n = (int)sqrt((double)n) + 1;
    int small_count = 0;
    int *small_primes = sieve_small_primes(sqrt_n, &small_count);

    /* is_prime flags for this process's local slice only, index 0 == value lo */
    char *is_prime = calloc((size_t)(local_n > 0 ? local_n : 1), sizeof(char));

    /* handle 2 explicitly since the loop below only walks odd numbers */
    if (lo <= 2 && 2 < hi) is_prime[2 - lo] = 1;

    /* start of the odd-number sweep within [lo, hi) */
    int odd_start = (lo % 2 == 0) ? lo + 1 : lo;
    if (odd_start < 3) odd_start = 3;

    #pragma omp parallel for schedule(dynamic, 64)
    for (int i = odd_start; i < hi; i += 2) {
        int prime = 1;
        double limit = sqrt((double)i);
        for (int p = 0; p < small_count; p++) {
            int d = small_primes[p];
            if (d == 2) continue; /* i is always odd here */
            if ((double)d > limit) break;
            if (i % d == 0) { prime = 0; break; }
        }
        is_prime[i - lo] = (char)prime;
    }

    /* ---- Build this process's sorted text buffer using per-thread buffers ---- */
    char **local_bufs = malloc((size_t)nthreads * sizeof(char *));
    size_t *local_lens = calloc((size_t)nthreads, sizeof(size_t));

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        size_t cap = ((size_t)(local_n > 0 ? local_n : 1) * 8) / (size_t)nthreads + 4096;
        char *lbuf = malloc(cap);
        size_t llen = 0;

        if (lbuf != NULL) {
            #pragma omp for schedule(static)
            for (int idx = 0; idx < local_n; idx++) {
                if (is_prime[idx]) {
                    llen += (size_t)snprintf(lbuf + llen, cap - llen, "%d\n", lo + idx);
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
    double compute_time = t_compute_end - t_start;

    /* ---- Gather every process's (already-sorted, contiguous-range) buffer ---- */
    int send_count = (int)off; /* assumes a single rank's byte count fits an int */
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

    double t_comm_end = MPI_Wtime();
    double comm_time = t_comm_end - t_compute_end;

    /* ---- Root writes the final, fully sorted result in one fwrite ---- */
    if (rank == 0) {
        const char *filename = "prime-hybrid.txt";
        FILE *fp = fopen(filename, "w");
        if (fp == NULL) {
            fprintf(stderr, "Error opening output file!\n");
        } else {
            fwrite(final_buf, 1, (size_t)total_len, fp);
            fclose(fp);
        }
        free(final_buf);
        free(recv_counts);
        free(displs);
    }

    /* ---- Reduce timings: the slowest rank sets the real wall-clock cost ---- */
    double max_compute, max_comm;
    MPI_Reduce(&compute_time, &max_compute, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&comm_time, &max_comm, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        double total_time = max_compute + max_comm;
        printf("n=%d  processes=%d  threads/process=%d\n", n, nprocs, nthreads);
        printf("compute time: %f s   gather time: %f s   total: %f s\n",
               max_compute, max_comm, total_time);

        FILE *log = fopen("timing_log_hybrid.csv", "a");
        if (log != NULL) {
            fprintf(log, "%d,%d,%d,%f,%f,%f\n",
                    n, nprocs, nthreads, max_compute, max_comm, total_time);
            fclose(log);
        }
    }

    MPI_Finalize();
    return 0;
}