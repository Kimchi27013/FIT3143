/*
Distributed Wireless Sensor Network (Lab 3) - shared definitions

Declares everything the EV node file (ev_node.c), the base station file (base_station.c)
and the simulation driver (wsn_sim.c) need to agree on: message tags, message layouts,
the simulation configuration and the public entry point of each "class".

Rank layout (world communicator):
  ranks 0 .. m*n-1 : EV charging nodes (grid rank == world rank, row-major)
  rank  m*n        : base station
*/

#ifndef WSN_H
#define WSN_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L   /* rand_r, nanosleep, localtime_r */
#endif

#include <stddef.h>
#include <time.h>
#include <mpi.h>

#define MAX_PORTS 64
#define MAX_NEIGHBOURS 4
#define MAX_CANDIDATES 8          /* at most 8 nodes sit at Manhattan distance 2 */
#define TIMESTAMP_LEN 32

/*
Message tags - one per message type so each type can be timed separately (Task 3)
Neighbour traffic uses the Cartesian grid communicator, base station traffic uses MPI_COMM_WORLD
*/
#define TAG_AVAIL_REQ 1   /* node -> neighbour      : availability request (grid comm)            */
#define TAG_AVAIL_REP 2   /* neighbour -> node      : availability reply (grid comm)              */
#define TAG_ALERT     3   /* node -> base station   : congestion report (world comm)              */
#define TAG_BS_REPLY  4   /* base station -> node   : nearest available nodes (world comm)        */
#define TAG_TERMINATE 5   /* base station -> node   : start graceful shutdown (world comm)        */
#define TAG_TERM_ACK  6   /* node -> base station   : shutdown complete + node stats (world comm) */

/* Base station reply status */
#define BS_STATUS_NONE_NEARBY 0
#define BS_STATUS_SUGGEST     1

/* Fixed-size int message layouts */
#define AVAIL_REPLY_LEN 3                    /* {rank, availability, totalPorts} */
#define BS_REPLY_LEN (2 + MAX_CANDIDATES)    /* {status, count, candidate ranks...} */

/* Node statistics carried by TAG_TERM_ACK (array of doubles) */
enum {
    ACK_ALERTS_SENT,
    ACK_POLLS,
    ACK_POLL_TIME,
    ACK_ALERT_RTT_COUNT,
    ACK_ALERT_RTT_TIME,
    ACK_MESSAGES,
    ACK_BYTES,
    ACK_LOCAL_REDIRECTS,
    TERM_ACK_LEN
};

/* Simulation parameters, identical on every rank (parsed from the command line) */
typedef struct {
    int rows;              /* m */
    int cols;              /* n */
    int portsPerNode;      /* k */
    double simSeconds;     /* how long the base station runs before terminating the network */
    int cycleMs;           /* port status update cycle */
    int threshold;         /* node is congested when availability < threshold */
    double busyProb;       /* probability a port is in use in a given cycle */
    double windowSeconds;  /* base station "predefined period" for recent reports */
} SimConfig;

/*
Congestion report sent from a node to the base station as ONE message (custom MPI struct type)
Field order matters: the char block, the double block and the int block must each be contiguous
*/
typedef struct {
    char timestamp[TIMESTAMP_LEN];        /* date & time the congestion was discovered */
    double detectWallTime;                /* same moment as epoch seconds */
    double pollCommTime;                  /* neighbour poll round trip (s) */
    int rank;
    int coords[2];
    int availability;                     /* free ports on the reporting node */
    int totalPorts;
    int neighbourCount;
    int neighbourRanks[MAX_NEIGHBOURS];
    int neighbourAvail[MAX_NEIGHBOURS];
    int messagesExchanged;                /* messages exchanged with neighbours for this incident */
    int bytesExchanged;                   /* size of those messages */
} NodeReport;

#define REPORT_INT_COUNT 16   /* rank .. bytesExchanged */

/* Implemented in ev_node.c */
void wsn_create_report_type(MPI_Datatype *reportType);
void wsn_timestamp(char *buffer, size_t length);
int evnode_run(const SimConfig *config, MPI_Comm gridComm, int baseRank);

/* Implemented in base_station.c */
int basestation_run(const SimConfig *config, int baseRank);

/*
Sleep helper used by the polling loops so they don't spin a core at 100%
The communication loops use a short sleep so it does not dominate the measured message delays (Task 3)
*/
#define COMM_LOOP_SLEEP_US 100
#define PORT_LOOP_SLEEP_US 10000

static inline void wsn_sleep_us(long microseconds)
{
    struct timespec duration;
    duration.tv_sec = microseconds / 1000000L;
    duration.tv_nsec = (microseconds % 1000000L) * 1000L;
    nanosleep(&duration, NULL);
}

#endif
