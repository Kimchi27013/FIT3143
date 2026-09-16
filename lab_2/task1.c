/*
Task 1 (parallel) - Finding Prime Numbers with MPI (Instrumented)

Compile:  mpicc -O2 -o task1 task1.c
Run:      mpirun -np 4 ./task1 100000000
*/

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

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
*AI Declaration: This function was written with AI
*/
static int compare_and_cast_int(const void *leftValue, const void *rightValue)
{
    int firstValue = *(const int *)leftValue;
    int secondValue = *(const int *)rightValue;
    return (firstValue > secondValue) - (firstValue < secondValue);
}

#define BLOCKS_PER_RANK 32

int main(int argc, char *argv[])
{
    int rankId, processCount;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rankId);
    MPI_Comm_size(MPI_COMM_WORLD, &processCount);

    /* SERIAL INITIALIZATION */
    double initStartTime = MPI_Wtime();

    int maxNumber = 0;
    if (rankId == 0) {
        if (argc < 2) {
            printf("Error: Please provide at least one argument.\n");
            printf("Usage: mpirun -np <procs> %s max_number\n", argv[0]);
            maxNumber = -1; // Error flag
        } else {
            maxNumber = atoi(argv[1]);
        }
    }

    // Broadcast n to all processes (creates measurable communication overhead)
    MPI_Bcast(&maxNumber, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (maxNumber < 2) {
        MPI_Finalize();
        return 1;
    }

    double initEndTime = MPI_Wtime();

    /* PARALLEL COMPONENT */
    double computeStartTime = MPI_Wtime();

    // Splitting range into small blocks, to then be taken in by each rank
    int range = (maxNumber > 2) ? (maxNumber - 2) : 0;
    int blockSize = range / (processCount * BLOCKS_PER_RANK);
    if (blockSize < 1) blockSize = 1;
    int numBlocks = (range + blockSize - 1) / blockSize;

    int localCapacity = 1024;
    int *localPrimes = malloc(localCapacity * sizeof(int));
    int localPrimeCount = 0;

    /* Blocks are assigned to ranks round-robin style for more even work distribution
    EG. Rank 0 processes blocks 0,4,8... & Rank 1 processes 1,5,9... so that all ranks process both
    larger and smaller primes, rather than processing only large or only small primes.
    *AI Declaration: This section was modified with AI.
    */
    for (int blockIndex = rankId; blockIndex < numBlocks; blockIndex += processCount) {
        int blockStart = 2 + blockIndex * blockSize;
        int blockEnd = blockStart + blockSize;
        if (blockEnd > maxNumber) blockEnd = maxNumber;

        for (int value = blockStart; value < blockEnd; value++) {
            if (is_prime(value)) {
                if (localPrimeCount == localCapacity) {
                    localCapacity *= 2;
                    localPrimes = realloc(localPrimes, localCapacity * sizeof(int));
                }
                localPrimes[localPrimeCount++] = value;
            }
        }
    }

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
    // NOTE: This part was modified with AI
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

    // Sorting final array as primes arrived out of order (blocks were assigned round-robin style)
    if (rankId == 0 && totalPrimes > 1) {
        qsort(allPrimes, totalPrimes, sizeof(int), compare_and_cast_int);
    }

    // Writing sorted array to file
    if (rankId == 0) {
        int writeToFile = (maxNumber > 100) ? 1 : 0;
        if (writeToFile) {
            const char *fileName = "prime-openmpi.txt";
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

        printf("\n--- Profiling Results (np=%d, n=%d) ---\n", processCount, maxNumber);
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