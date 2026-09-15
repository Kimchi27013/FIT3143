/*
Task 1 (parallel) - Finding Prime Numbers with MPI

The range [2, n) is cut into many small blocks and the blocks are
handed out round-robin ("block-cyclic") across ranks: rank r owns
blocks r, r+size, r+2*size, ... Each rank tests every number inside
its own blocks and keeps the local primes it finds. Rank 0 gathers
everyone's results, sorts them, and prints/writes the final list.

Why block-cyclic instead of one contiguous block per rank?
Trial-division cost grows with sqrt(num), so numbers near n cost far
more to test than numbers near 2. Handing each rank one contiguous
slice therefore gives the highest-numbered rank most of the real
work even though every rank gets the same *count* of numbers -- that
rank becomes the bottleneck and caps the achievable speedup.
Scattering many small blocks round-robin across the whole range
mixes cheap (small) and expensive (large) numbers into every rank's
share, so total *work*, not just element count, is balanced.

Why *block*-cyclic and not plain element-by-element cyclic
(i.e. "rank r tests 2+r, 2+r+size, 2+r+2*size, ...")?
is_prime() below uses the standard 6k+-1 wheel: after ruling out
multiples of 2 and 3 with one cheap check, it only needs to fully
trial-divide candidates congruent to 1 or 5 (mod 6) -- primes can
only be of that form. If the stride between a rank's numbers (i.e.
`size`, the process count) shares a factor with 6, that rank keeps
landing on the *same* residue class mod 6 forever: some ranks would
then get nothing but expensive candidates and others nothing but
instant composite rejects, which is arguably worse than the original
contiguous-block imbalance (measured: with np=6, raw element-cyclic
striping left two ranks doing ~500x more work than the rest, because
6 % 6 == 0). Using contiguous blocks that are much larger than 6
avoids this resonance entirely, since every block naturally contains
a representative mix of residues mod 6, while still scattering blocks
across the full [2, n) range for the coarse-grained balance described
above.

The trade-off: numbers within one rank's blocks are still increasing,
but blocks from different ranks are interleaved, so the concatenated
gathered array is not sorted end-to-end -- an explicit sort at rank 0
is required (cheap relative to the parallel testing phase it follows).

Compile:  mpicc -O2 -o task1 task1.c
Run:      mpirun -np 4 ./task1 1000000
*/

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

// 6k +/- 1 trial division: once multiples of 2 and 3 are removed,
// every remaining prime candidate has the form 6k+1 or 6k+5, so only
// those divisors need to be tested. This roughly halves the number
// of candidate divisors compared to "skip only even numbers", and
// comparing j*j <= num (integer multiply) instead of num/j (integer
// divide) avoids a division on every loop iteration.
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

// Target number of blocks per rank. Higher = finer-grained balancing
// (better averages out the sqrt(num) cost gradient) at the cost of a
// little extra loop overhead. 32 is a good default for n in the
// millions-to-billions range typical of this kind of assignment.
#define BLOCKS_PER_RANK 32

int main(int argc, char *argv[])
{
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc < 2) {
        if (rank == 0) {
            printf("Error: Please provide at least one argument.\n");
            printf("Usage: mpirun -np <procs> %s max_number\n", argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    int n = atoi(argv[1]);
    MPI_Barrier(MPI_COMM_WORLD);
    double start = MPI_Wtime();

    int range = (n > 2) ? (n - 2) : 0;
    int block_size = range / (size * BLOCKS_PER_RANK);
    if (block_size < 1) block_size = 1;
    int num_blocks = (range + block_size - 1) / block_size; // ceil

    // Growable buffer: with block-cyclic scheduling the exact count a
    // rank will find isn't known in advance, so grow on demand instead
    // of trying to pre-compute a tight capacity.
    int local_capacity = 1024;
    int *local_primes = malloc(local_capacity * sizeof(int));
    int local_prime_count = 0;

    // Rank r owns blocks r, r+size, r+2*size, ... (block-cyclic).
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

    // Gather how many primes each rank found, then gather the values
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

    // Each rank's own numbers are increasing, but blocks from
    // different ranks are interleaved across the gathered array, so
    // it needs an explicit sort before it is valid sorted output.
    if (rank == 0 && total_primes > 1) {
        qsort(all_primes, total_primes, sizeof(int), cmp_int);
    }

    double end = MPI_Wtime();

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
                printf("Wrote %d primes to %s\n", total_primes, filename);
            }
        } else {
            for (int i = 0; i < total_primes; i++) {
                printf("%d\n", all_primes[i]);
            }
        }

        printf("Wall clock time used: %f seconds (np=%d)\n", end - start, size);

        free(all_primes);
        free(recv_counts);
        free(displs);
    }

    free(local_primes);
    MPI_Finalize();
    return 0;
}