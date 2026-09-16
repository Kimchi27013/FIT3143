/*
Task 2 (Hybrid MPI + OpenMP version) - Finding Prime Numbers with MPI+OpenMP (Instrumented)

Same is_prime() and round-robin block assignment as task1 — the only addition
here is parallelizing each rank's block loop across OpenMP threads.

Compile:  mpicc -O2 -fopenmp -o task2 task2.c -lm
Run:      mpirun -np 4 ./task2 100000000 4
          (second arg = threads per process, optional)
*/

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <math.h>
#include <omp.h>

/*
Primality function
Checks divisors of the form 6k+1 and up to sqrt(num) as all primes above three are of this format
*/
static inline int is_prime(int num)
{
    if (num < 2) return 0;
    if (num == 2 || num == 3) return 1;
    if (num % 2 == 0 || num % 3 == 0) return 0;

    for (int divisor = 5; (long long)divisor * divisor <= num; divisor += 6) {
        if (num % divisor == 0 || num % (divisor + 2) == 0) return 0;
    }
    return 1;
}

/*
Utility function
Casts pointers to integers and compares them 
*NOTE: This function was written with AI
*/
static int compare_and_cast_int(const void *leftValue, const void *rightValue)
{
    int firstValue = *(const int *)leftValue;
    int secondValue = *(const int *)rightValue;
    return (firstValue > secondValue) - (firstValue < secondValue);
}

// Base block count per rank (task1's original value)
#define BLOCKS_PER_RANK_BASE 32

int main(int argc, char *argv[])
{
    int rankId, processCount;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rankId);
    MPI_Comm_size(MPI_COMM_WORLD, &processCount);

    /* SERIAL INITIALIZATION */
    double initStartTime = MPI_Wtime();

    int maxNumber = 0;
    int requestedThreads = 0;
    if (rankId == 0) {
        if (argc < 2) {
            printf("Error: Please provide at least one argument.\n");
            printf("Usage: mpirun -np <procs> %s max_number [threads_per_process]\n", argv[0]);
            maxNumber = -1; // Error flag
        } else {
            maxNumber = atoi(argv[1]);
            if (argc >= 3) requestedThreads = atoi(argv[2]);
        }
    }

    // Broadcast n and the requested thread count to all processes
    // (creates measurable communication overhead, same as task1)
    MPI_Bcast(&maxNumber, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&requestedThreads, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (maxNumber < 2) {
        MPI_Finalize();
        return 1;
    }

    // Set OpenMP thread count per process if the user asked for a specific
    // number, otherwise use OMP_NUM_THREADS
    if (requestedThreads > 0) omp_set_num_threads(requestedThreads);
    int threadCount = omp_get_max_threads();

    double initEndTime = MPI_Wtime();

    /* PARALLEL COMPONENT */
    double computeStartTime = MPI_Wtime();

    // Scale total block count by thread count too, not just rank count, so
    // each rank has enough blocks (BLOCKS_PER_RANK_BASE per thread) to keep
    // every OpenMP thread fed under dynamic scheduling. Without this, a rank
    // with e.g. 4 threads but only ~32 blocks total gives threads too few
    // chances to rebalance against each other.
    // *NOTE: This was written with AI
    int blocksPerRank = BLOCKS_PER_RANK_BASE * threadCount;

    int range = (maxNumber > 2) ? (maxNumber - 2) : 0;
    int blockSize = range / (processCount * blocksPerRank);
    if (blockSize < 1) blockSize = 1;
    int numBlocks = (range + blockSize - 1) / blockSize;

    // Work out which blocks belong to this rank (round-robin, same as task1),
    // so OpenMP threads below can be handed out chunks of THIS rank's blocks
    int rankBlockCount = 0;
    for (int blockIndex = rankId; blockIndex < numBlocks; blockIndex += processCount) rankBlockCount++;

    int *rankBlocks = malloc((rankBlockCount > 0 ? rankBlockCount : 1) * sizeof(int));
    {
        int index = 0;
        for (int blockIndex = rankId; blockIndex < numBlocks; blockIndex += processCount) rankBlocks[index++] = blockIndex;
    }

    // Rough estimate of how many primes this rank will find, split evenly
    // across threads, used to pre-size each thread's buffer up front and
    // cut down on realloc() churn/contention during the parallel region.
    // Prime counting function pi(x) ~ x / ln(x); we pad generously since
    // it's only a starting guess, not a hard cap (buffers still grow via
    // realloc if the estimate is too low).
    // *NOTE: This was written with AI
    double rangeHi = (double)((rankId + 1) * range) / (double)(numBlocks > 0 ? numBlocks : 1);
    double approxLn = (rangeHi > 2.0) ? log(rangeHi) : 1.0;
    int estRankPrimes = (int)((double)(rankBlockCount * blockSize) / approxLn) + 64;
    int estThreadPrimes = (estRankPrimes / (threadCount > 0 ? threadCount : 1)) + 64;

    // Each thread gets its own growable buffer of found primes, to avoid
    // lock contention from multiple threads writing to one shared array
    int **threadPrimes = malloc((size_t)threadCount * sizeof(int *));
    int *threadCounts = calloc((size_t)threadCount, sizeof(int));
    int *threadCaps = malloc((size_t)threadCount * sizeof(int));

    #pragma omp parallel
    {
        int threadId = omp_get_thread_num();
        int capacity = estThreadPrimes; // pre-sized estimate instead of a fixed 1024
        int *buffer = malloc(capacity * sizeof(int));
        int count = 0;

        /* Blocks are assigned to ranks round-robin style for more even work distribution
        EG. Rank 0 processes blocks 0,4,8... & Rank 1 processes 1,5,9... so that all ranks process both
        larger and smaller primes, rather than processing only large or only small primes.
        Within a rank, its blocks are further handed out to OpenMP threads dynamically
        since dynamic scheduling lets idle threads pick up more blocks instead of sitting idle.
        *NOTE: This section was modified with AI.
        */
        #pragma omp for schedule(dynamic, 1)
        for (int index = 0; index < rankBlockCount; index++) {
            int blockIndex = rankBlocks[index];
            int blockStart = 2 + blockIndex * blockSize;
            int blockEnd = blockStart + blockSize;
            if (blockEnd > maxNumber) blockEnd = maxNumber;

            for (int value = blockStart; value < blockEnd; value++) {
                if (is_prime(value)) {
                    if (count == capacity) {
                        capacity *= 2;
                        buffer = realloc(buffer, capacity * sizeof(int));
                    }
                    buffer[count++] = value;
                }
            }
        }

        threadPrimes[threadId] = buffer;
        threadCounts[threadId] = count;
        threadCaps[threadId] = capacity;
    }

    // Merge all thread-private buffers into one array for this rank,
    // same role as localPrimes in task1
    int localPrimeCount = 0;
    for (int threadIndex = 0; threadIndex < threadCount; threadIndex++) localPrimeCount += threadCounts[threadIndex];

    int *localPrimes = malloc((localPrimeCount > 0 ? localPrimeCount : 1) * sizeof(int));
    {
        int offset = 0;
        for (int threadIndex = 0; threadIndex < threadCount; threadIndex++) {
            for (int value = 0; value < threadCounts[threadIndex]; value++) {
                localPrimes[offset++] = threadPrimes[threadIndex][value];
            }
            free(threadPrimes[threadIndex]);
        }
    }
    free(threadPrimes);
    free(threadCounts);
    free(threadCaps);
    free(rankBlocks);

    double computeEndTime = MPI_Wtime();

    /* SERIAL GATHER, SORT AND WRITE */
    double commStartTime = MPI_Wtime();

    int *recvCounts = NULL;
    int *offsets = NULL;
    if (rankId == 0) {
        recvCounts = malloc(processCount * sizeof(int));
        offsets = malloc(processCount * sizeof(int));
    }

    // Collects each rank's prime count into receive buffer
    MPI_Gather(&localPrimeCount, 1, MPI_INT, recvCounts, 1, MPI_INT, 0, MPI_COMM_WORLD);

    int totalPrimes = 0;
    int *allPrimes = NULL;
    // Create table of offsets for receive buffer to be used by MPI_Gatherv
    if (rankId == 0) {
        offsets[0] = 0;
        for (int index = 1; index < processCount; index++) {
            offsets[index] = offsets[index - 1] + recvCounts[index - 1];
        }
        totalPrimes = offsets[processCount - 1] + recvCounts[processCount - 1];
        allPrimes = malloc((totalPrimes > 0 ? totalPrimes : 1) * sizeof(int));
    }

    // Creating final array
    MPI_Gatherv(localPrimes, localPrimeCount, MPI_INT,
                allPrimes, recvCounts, offsets, MPI_INT,
                0, MPI_COMM_WORLD);

    // Sorting final array as primes arrived out of order (blocks assigned round-robin
    // across ranks, and within each rank, across threads too)
    if (rankId == 0 && totalPrimes > 1) {
        qsort(allPrimes, totalPrimes, sizeof(int), compare_and_cast_int);
    }

    // Writing sorted array to file
    if (rankId == 0) {
        int writeToFile = (maxNumber > 100) ? 1 : 0;
        if (writeToFile) {
            const char *fileName = "prime-openmpi-hybrid.txt";
            FILE *filePointer = fopen(fileName, "w");
            if (filePointer == NULL) {
                printf("Error opening file!\n");
            } else {
                for (int value = 0; value < totalPrimes; value++) {
                    fprintf(filePointer, "%d\n", allPrimes[value]);
                }
                fclose(filePointer);
            }
        }
        // Free rank 0 related metadata
        free(allPrimes);
        free(recvCounts);
        free(offsets);
    }
    // Freeing the other ranks' metadata
    free(localPrimes);

    double commEndTime = MPI_Wtime();

    /*  CODE FOR THE TIMING ANALYSIS
        NOTE: This part was written using AI
    */
    double localInitTime = initEndTime - initStartTime;
    double localComputeTime = computeEndTime - computeStartTime;
    double localCommTime = commEndTime - commStartTime;

    double maxInit, maxCompute, maxComm;

    // We use MPI_MAX to find the slowest process in each phase,
    // which represents the true "wall-clock" limit.
    MPI_Reduce(&localInitTime, &maxInit, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&localComputeTime, &maxCompute, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&localCommTime, &maxComm, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rankId == 0) {
        double totalSerial = maxInit + maxComm;
        double totalTime = totalSerial + maxCompute;

        printf("\n--- Profiling Results (np=%d, threads=%d, n=%d) ---\n", processCount, threadCount, maxNumber);
        printf("T_serial_init : %f s\n", maxInit);
        printf("T_parallel    : %f s\n", maxCompute);
        printf("T_serial_comm : %f s\n", maxComm);
        printf("--------------------------------------\n");
        printf("Total Serial (T_s)  : %f s\n", totalSerial);
        printf("Total Parallel (T_p): %f s\n", maxCompute);
        printf("Total Execution     : %f s\n", totalTime);

        // These are the exact fractions you need for Amdahl's/Gustafson's Laws!
        printf("Serial Fraction (f) : %f\n", totalSerial / totalTime);
        printf("Parallel Fraction   : %f\n", maxCompute / totalTime);
    }

    MPI_Finalize();
    return 0;
}