/*
Distributed Wireless Sensor Network simulation (Lab 3) - driver

Sets up MPI (with thread support for OpenMP), splits the base station off from the
charging nodes, builds the Cartesian grid virtual topology and starts each role.

Compile:  make            (or: mpicc -O2 -fopenmp -o wsn wsn_sim.c ev_node.c base_station.c)
Run:      mpirun --oversubscribe -np <m*n+1> ./wsn m n [ports] [simSeconds] [cycleMs] [threshold] [busyProb] [windowSeconds]
Example:  mpirun --oversubscribe -np 10 ./wsn 3 3 6 30 1000 2 0.75 10
Line:     mpirun --oversubscribe -np 9 ./wsn 8 1         (line topology for Task 3b)

Outputs:  base_station.log, node_<rank>.log, timing_results.csv (one row appended per run)
*/

#include "wsn.h"

#include <stdio.h>
#include <stdlib.h>

static void print_usage(const char *program)
{
    printf("Usage: mpirun -np <m*n+1> %s m n [ports=6] [simSeconds=30] [cycleMs=1000] [threshold=2] [busyProb=0.75] [windowSeconds=10]\n",
           program);
}

/* Parses positional arguments into config; returns 0 if valid */
static int parse_config(int argc, char *argv[], SimConfig *config)
{
    config->rows = 0;
    config->cols = 0;
    config->portsPerNode = 6;
    config->simSeconds = 30.0;
    config->cycleMs = 1000;
    config->threshold = 2;
    config->busyProb = 0.75;
    config->windowSeconds = 10.0;

    if (argc < 3) return -1;
    config->rows = atoi(argv[1]);
    config->cols = atoi(argv[2]);
    if (argc > 3) config->portsPerNode = atoi(argv[3]);
    if (argc > 4) config->simSeconds = atof(argv[4]);
    if (argc > 5) config->cycleMs = atoi(argv[5]);
    if (argc > 6) config->threshold = atoi(argv[6]);
    if (argc > 7) config->busyProb = atof(argv[7]);
    if (argc > 8) config->windowSeconds = atof(argv[8]);

    if (config->rows < 1 || config->cols < 1) return -1;
    if (config->portsPerNode < 1 || config->portsPerNode > MAX_PORTS) return -1;
    if (config->simSeconds <= 0.0 || config->cycleMs < 1) return -1;
    if (config->threshold < 0 || config->threshold > config->portsPerNode) return -1;
    if (config->busyProb < 0.0 || config->busyProb > 1.0) return -1;
    if (config->windowSeconds < 0.0) return -1;
    return 0;
}

int main(int argc, char *argv[])
{
    /* FUNNELED: only the thread that called MPI_Init_thread (OpenMP master) makes MPI calls */
    int threadSupport;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &threadSupport);

    int worldRank, worldSize;
    MPI_Comm_rank(MPI_COMM_WORLD, &worldRank);
    MPI_Comm_size(MPI_COMM_WORLD, &worldSize);

    SimConfig config;
    int configError = parse_config(argc, argv, &config);

    /* Every rank validates the same inputs, so they all exit together without MPI_Abort */
    if (configError != 0 || threadSupport < MPI_THREAD_FUNNELED || worldSize != config.rows * config.cols + 1) {
        if (worldRank == 0) {
            if (threadSupport < MPI_THREAD_FUNNELED) {
                printf("Error: this MPI library does not provide MPI_THREAD_FUNNELED\n");
            } else if (configError != 0) {
                printf("Error: invalid arguments\n");
            } else {
                printf("Error: %d x %d grid needs %d processes (nodes + base station), got %d\n",
                       config.rows, config.cols, config.rows * config.cols + 1, worldSize);
            }
            print_usage(argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    int baseRank = worldSize - 1;
    int isBaseStation = worldRank == baseRank;

    /* Separate the charging nodes from the base station; key = world rank keeps grid rank == world rank */
    MPI_Comm nodeComm;
    MPI_Comm_split(MPI_COMM_WORLD, isBaseStation ? MPI_UNDEFINED : 0, worldRank, &nodeComm);

    int result;
    if (isBaseStation) {
        printf("WSN simulation: %d x %d grid, %d ports/node, %.1f s, base station = rank %d\n",
               config.rows, config.cols, config.portsPerNode, config.simSeconds, baseRank);
        result = basestation_run(&config, baseRank);
    } else {
        /* Cartesian virtual topology, non-periodic (no wraparound), no reordering */
        int dims[2] = {config.rows, config.cols};
        int periods[2] = {0, 0};
        MPI_Comm gridComm;
        MPI_Cart_create(nodeComm, 2, dims, periods, 0, &gridComm);

        result = evnode_run(&config, gridComm, baseRank);

        MPI_Comm_free(&gridComm);
    }

    if (nodeComm != MPI_COMM_NULL) MPI_Comm_free(&nodeComm);

    MPI_Finalize();
    return result;
}
