/*
EV charging node (Lab 3, Task 1)

Contains the "classes" used by every charging node:
  - ChargingPort : simulated by OpenMP threads, each port updates its in-use/free status every cycle
  - NodeReport   : congestion report sent to the base station as one custom MPI struct message
  - EVNode       : one MPI process per node, the master thread performs ALL MPI communication

Applied 1 review fixes in this file:
  (1) The node polls its own neighbours first, redirects EVs locally if a neighbour has vacancies,
      and only alerts the base station when every neighbour is also congested
  (2) A receive for neighbour requests is ALWAYS posted and serviced while the node waits on its own
      poll (MPI_Testall loop instead of MPI_Waitall), so two congested neighbours cannot deadlock
  (3) Neighbours come from MPI_Cart_shift on the grid communicator (MPI_PROC_NULL at edges),
      one tag per message type, MPI_Isend instead of MPI_Ibsend / blocking alerts,
      and a single packed NodeReport per alert
  (4) MPI_THREAD_FUNNELED: port threads never call MPI, each port thread writes only its own
      slot of the shared port array (atomic read/write), shutdown uses a shared running flag
  (5) Graceful shutdown: stop polling, keep answering neighbours until a grid MPI_Ibarrier
      completes, cancel the pending receive, wait on all sends, join threads, free everything
*/

#include "wsn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>

#define PORT_FREE 0
#define PORT_IN_USE 1

enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };
static const char *directionNames[MAX_NEIGHBOURS] = {"up", "down", "left", "right"};

_Static_assert(offsetof(NodeReport, bytesExchanged) - offsetof(NodeReport, rank) == (REPORT_INT_COUNT - 1) * sizeof(int),
               "NodeReport int fields must be contiguous for the MPI struct type");
_Static_assert(offsetof(NodeReport, pollCommTime) - offsetof(NodeReport, detectWallTime) == sizeof(double),
               "NodeReport double fields must be contiguous for the MPI struct type");

/* ======================================================================
   ChargingPort
   ====================================================================== */

typedef struct {
    int portId;
    int status;           /* PORT_FREE / PORT_IN_USE - written only by the owning thread */
    unsigned int seed;    /* private RNG state for rand_r (rand() is not thread safe) */
} ChargingPort;

static void port_init(ChargingPort *port, int portId, int nodeRank)
{
    port->portId = portId;
    port->status = PORT_FREE;
    port->seed = (unsigned int)time(NULL) ^ (unsigned int)(nodeRank * 7919) ^ (unsigned int)(portId * 104729);
}

/*
Simulates EV arrival/departure for one cycle
The port is in use this cycle with probability busyProb
*/
static void port_simulate_cycle(ChargingPort *port, double busyProb)
{
    double draw = (double)rand_r(&port->seed) / RAND_MAX;
    int newStatus = draw < busyProb ? PORT_IN_USE : PORT_FREE;

    #pragma omp atomic write
    port->status = newStatus;
}

static int port_get_status(ChargingPort *port)
{
    int status;

    #pragma omp atomic read
    status = port->status;

    return status;
}

/* ======================================================================
   NodeReport helpers (shared with the base station through wsn.h)
   ====================================================================== */

/*
Builds the MPI datatype for NodeReport: one char block, one double block, one int block
Resized to sizeof(NodeReport) so trailing padding is handled correctly
*/
void wsn_create_report_type(MPI_Datatype *reportType)
{
    int blockLengths[3] = {TIMESTAMP_LEN, 2, REPORT_INT_COUNT};
    MPI_Aint displacements[3] = {
        offsetof(NodeReport, timestamp),
        offsetof(NodeReport, detectWallTime),
        offsetof(NodeReport, rank)
    };
    MPI_Datatype blockTypes[3] = {MPI_CHAR, MPI_DOUBLE, MPI_INT};
    MPI_Datatype structType;

    MPI_Type_create_struct(3, blockLengths, displacements, blockTypes, &structType);
    MPI_Type_create_resized(structType, 0, sizeof(NodeReport), reportType);
    MPI_Type_commit(reportType);
    MPI_Type_free(&structType);
}

/* Formats the current local date & time as "YYYY-MM-DD HH:MM:SS" */
void wsn_timestamp(char *buffer, size_t length)
{
    time_t now = time(NULL);
    struct tm localTime;
    localtime_r(&now, &localTime);
    strftime(buffer, length, "%Y-%m-%d %H:%M:%S", &localTime);
}

/* ======================================================================
   EVNode
   ====================================================================== */

typedef struct {
    const SimConfig *config;
    MPI_Comm gridComm;
    int baseRank;                                   /* world rank of the base station */
    int rank;                                       /* grid rank (== world rank) */
    int coords[2];
    int neighbourRanks[MAX_NEIGHBOURS];             /* up, down, left, right - MPI_PROC_NULL at edges */
    int neighbourCount;

    ChargingPort ports[MAX_PORTS];                  /* shared array, one slot per port thread */
    int running;                                    /* shared flag read by the port threads */

    FILE *logFile;
    MPI_Datatype reportType;

    /* Incoming neighbour requests - this receive is ALWAYS posted (fix 2) */
    int incomingRequestBuf;
    MPI_Request incomingRequest;

    /* Replies to neighbours - at most one outstanding per direction */
    int replyBuf[MAX_NEIGHBOURS][AVAIL_REPLY_LEN];
    MPI_Request replyRequests[MAX_NEIGHBOURS];

    /* This node's own neighbour poll */
    int polling;
    int pollAvailability;                           /* own availability when the poll started */
    double pollStartTime;
    int pollSendBuf;
    int pollRecvBuf[MAX_NEIGHBOURS][AVAIL_REPLY_LEN];
    MPI_Request pollRequests[2 * MAX_NEIGHBOURS];   /* [0..3] receives, [4..7] sends */

    /* Alert to the base station */
    NodeReport report;                              /* must stay untouched until alertRequest completes */
    MPI_Request alertRequest;
    int awaitingBaseReply;
    double alertSentTime;

    /* Shutdown */
    int terminating;
    int barrierPosted;
    MPI_Request barrierRequest;

    /* Statistics (reported to the base station in the termination ACK) */
    int cycleCount;
    int alertsSent;
    int pollCount;
    double pollTimeTotal;
    int alertRttCount;
    double alertRttTotal;
    long messageCount;
    long byteCount;
    int localRedirects;
} EVNode;

static int evnode_is_running(EVNode *node)
{
    int running;

    #pragma omp atomic read
    running = node->running;

    return running;
}

static int evnode_count_available(EVNode *node)
{
    int available = 0;
    for (int p = 0; p < node->config->portsPerNode; p++) {
        if (port_get_status(&node->ports[p]) == PORT_FREE) available++;
    }
    return available;
}

/* Maps a neighbour's grid rank to its direction index (-1 if it is not a neighbour) */
static int evnode_direction_of(const EVNode *node, int neighbourRank)
{
    for (int d = 0; d < MAX_NEIGHBOURS; d++) {
        if (node->neighbourRanks[d] == neighbourRank) return d;
    }
    return -1;
}

static void evnode_post_request_receive(EVNode *node)
{
    MPI_Irecv(&node->incomingRequestBuf, 1, MPI_INT, MPI_ANY_SOURCE, TAG_AVAIL_REQ,
              node->gridComm, &node->incomingRequest);
}

static int evnode_init(EVNode *node, const SimConfig *config, MPI_Comm gridComm, int baseRank)
{
    node->config = config;
    node->gridComm = gridComm;
    node->baseRank = baseRank;
    node->running = 1;

    MPI_Comm_rank(gridComm, &node->rank);
    MPI_Cart_coords(gridComm, node->rank, 2, node->coords);

    /* Dimension 0 = rows (up/down), dimension 1 = columns (left/right) */
    MPI_Cart_shift(gridComm, 0, 1, &node->neighbourRanks[DIR_UP], &node->neighbourRanks[DIR_DOWN]);
    MPI_Cart_shift(gridComm, 1, 1, &node->neighbourRanks[DIR_LEFT], &node->neighbourRanks[DIR_RIGHT]);

    node->neighbourCount = 0;
    for (int d = 0; d < MAX_NEIGHBOURS; d++) {
        if (node->neighbourRanks[d] != MPI_PROC_NULL) node->neighbourCount++;
        node->replyRequests[d] = MPI_REQUEST_NULL;
    }
    for (int r = 0; r < 2 * MAX_NEIGHBOURS; r++) node->pollRequests[r] = MPI_REQUEST_NULL;
    node->alertRequest = MPI_REQUEST_NULL;
    node->barrierRequest = MPI_REQUEST_NULL;
    node->pollSendBuf = node->rank;

    for (int p = 0; p < config->portsPerNode; p++) port_init(&node->ports[p], p, node->rank);

    char fileName[64];
    snprintf(fileName, sizeof(fileName), "node_%d.log", node->rank);
    node->logFile = fopen(fileName, "w");
    if (node->logFile == NULL) {
        fprintf(stderr, "Node %d: could not open %s\n", node->rank, fileName);
        return -1;
    }

    wsn_create_report_type(&node->reportType);

    fprintf(node->logFile, "EV charging node %d at (%d,%d), %d ports, %d neighbour(s):",
            node->rank, node->coords[0], node->coords[1], config->portsPerNode, node->neighbourCount);
    for (int d = 0; d < MAX_NEIGHBOURS; d++) {
        if (node->neighbourRanks[d] != MPI_PROC_NULL) {
            fprintf(node->logFile, " %s=%d", directionNames[d], node->neighbourRanks[d]);
        }
    }
    fprintf(node->logFile, "\n\nYear Month Day Hour Minute Second Availability\n");

    evnode_post_request_receive(node);
    return 0;
}

/* Writes one Table 1 style row (date/time + availability) for this cycle */
static void evnode_record_cycle(EVNode *node, int availability)
{
    time_t now = time(NULL);
    struct tm localTime;
    localtime_r(&now, &localTime);

    fprintf(node->logFile, "%04d  %02d  %02d  %02d  %02d  %02d  %d\n",
            localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday,
            localTime.tm_hour, localTime.tm_min, localTime.tm_sec, availability);
    node->cycleCount++;
}

/*
Answers every neighbour availability request that has arrived (fix 2)
Called on every loop iteration, including while this node's own poll is outstanding
*/
static void evnode_service_requests(EVNode *node)
{
    int arrived = 0;
    MPI_Status status;

    MPI_Test(&node->incomingRequest, &arrived, &status);
    while (arrived) {
        int requester = status.MPI_SOURCE;
        int direction = evnode_direction_of(node, requester);

        if (direction >= 0) {
            /* The requester only polls again after receiving our last reply, so this completes immediately */
            MPI_Wait(&node->replyRequests[direction], MPI_STATUS_IGNORE);

            node->replyBuf[direction][0] = node->rank;
            node->replyBuf[direction][1] = evnode_count_available(node);
            node->replyBuf[direction][2] = node->config->portsPerNode;
            MPI_Isend(node->replyBuf[direction], AVAIL_REPLY_LEN, MPI_INT, requester, TAG_AVAIL_REP,
                      node->gridComm, &node->replyRequests[direction]);

            node->messageCount += 2;
            node->byteCount += (long)sizeof(int) * (1 + AVAIL_REPLY_LEN);
        } else {
            fprintf(node->logFile, "WARNING: availability request from non-neighbour %d ignored\n", requester);
        }

        evnode_post_request_receive(node);
        MPI_Test(&node->incomingRequest, &arrived, &status);
    }
}

/*
Starts a non-blocking availability poll of all four directions
Sends/receives to MPI_PROC_NULL (grid edges) complete immediately and leave the buffer untouched
*/
static void evnode_start_poll(EVNode *node, int availability)
{
    node->polling = 1;
    node->pollAvailability = availability;
    node->pollStartTime = MPI_Wtime();

    for (int d = 0; d < MAX_NEIGHBOURS; d++) {
        node->pollRecvBuf[d][0] = MPI_PROC_NULL;
        node->pollRecvBuf[d][1] = -1;
        node->pollRecvBuf[d][2] = 0;
        MPI_Irecv(node->pollRecvBuf[d], AVAIL_REPLY_LEN, MPI_INT, node->neighbourRanks[d], TAG_AVAIL_REP,
                  node->gridComm, &node->pollRequests[d]);
    }
    for (int d = 0; d < MAX_NEIGHBOURS; d++) {
        MPI_Isend(&node->pollSendBuf, 1, MPI_INT, node->neighbourRanks[d], TAG_AVAIL_REQ,
                  node->gridComm, &node->pollRequests[MAX_NEIGHBOURS + d]);
    }
}

/* Packs everything about the incident into ONE NodeReport and sends it without blocking */
static void evnode_send_alert(EVNode *node, double pollTime, int pollMessages, int pollBytes)
{
    /* Previous alert was answered already, so its send has finished - wait just releases the request */
    MPI_Wait(&node->alertRequest, MPI_STATUS_IGNORE);

    NodeReport *report = &node->report;
    memset(report, 0, sizeof(NodeReport));
    wsn_timestamp(report->timestamp, TIMESTAMP_LEN);
    report->detectWallTime = (double)time(NULL);
    report->pollCommTime = pollTime;
    report->rank = node->rank;
    report->coords[0] = node->coords[0];
    report->coords[1] = node->coords[1];
    report->availability = node->pollAvailability;
    report->totalPorts = node->config->portsPerNode;

    int reported = 0;
    for (int d = 0; d < MAX_NEIGHBOURS; d++) {
        if (node->neighbourRanks[d] == MPI_PROC_NULL) continue;
        report->neighbourRanks[reported] = node->neighbourRanks[d];
        report->neighbourAvail[reported] = node->pollRecvBuf[d][1];
        reported++;
    }
    report->neighbourCount = reported;
    report->messagesExchanged = pollMessages;
    report->bytesExchanged = pollBytes;

    MPI_Isend(report, 1, node->reportType, node->baseRank, TAG_ALERT, MPI_COMM_WORLD, &node->alertRequest);

    node->awaitingBaseReply = 1;
    node->alertSentTime = MPI_Wtime();
    node->alertsSent++;
    node->messageCount++;
    node->byteCount += (long)sizeof(NodeReport);

    fprintf(node->logFile, "[%s] ALERT sent to base station: own availability %d/%d, all neighbours below threshold %d\n",
            report->timestamp, report->availability, report->totalPorts, node->config->threshold);
}

/* Checks whether the outstanding poll has finished and acts on the result */
static void evnode_check_poll(EVNode *node)
{
    int complete = 0;
    MPI_Testall(2 * MAX_NEIGHBOURS, node->pollRequests, &complete, MPI_STATUSES_IGNORE);
    if (!complete) return;

    double pollTime = MPI_Wtime() - node->pollStartTime;
    int pollMessages = 2 * node->neighbourCount;
    int pollBytes = node->neighbourCount * (int)sizeof(int) * (1 + AVAIL_REPLY_LEN);

    node->polling = 0;
    node->pollCount++;
    node->pollTimeTotal += pollTime;
    node->messageCount += pollMessages;
    node->byteCount += pollBytes;

    if (node->terminating) return;   /* shutting down - result no longer acted upon */

    /* Pick the neighbour with the most free ports that is not itself congested */
    int bestDirection = -1;
    int bestAvailability = -1;
    for (int d = 0; d < MAX_NEIGHBOURS; d++) {
        if (node->neighbourRanks[d] == MPI_PROC_NULL) continue;
        int availability = node->pollRecvBuf[d][1];
        if (availability >= node->config->threshold && availability > bestAvailability) {
            bestAvailability = availability;
            bestDirection = d;
        }
    }

    char timestamp[TIMESTAMP_LEN];
    wsn_timestamp(timestamp, sizeof(timestamp));

    if (bestDirection >= 0) {
        node->localRedirects++;
        fprintf(node->logFile, "[%s] Congested (%d free) - redirecting incoming EVs to neighbour %d (%s) with %d free port(s)\n",
                timestamp, node->pollAvailability, node->neighbourRanks[bestDirection],
                directionNames[bestDirection], bestAvailability);
    } else {
        evnode_send_alert(node, pollTime, pollMessages, pollBytes);
    }
}

/* Handles base station replies and the termination message (non-blocking probe) */
static void evnode_check_base_messages(EVNode *node)
{
    int arrived = 0;
    MPI_Status status;

    MPI_Iprobe(node->baseRank, MPI_ANY_TAG, MPI_COMM_WORLD, &arrived, &status);
    if (!arrived) return;

    char timestamp[TIMESTAMP_LEN];
    wsn_timestamp(timestamp, sizeof(timestamp));

    if (status.MPI_TAG == TAG_BS_REPLY) {
        int reply[BS_REPLY_LEN];
        MPI_Recv(reply, BS_REPLY_LEN, MPI_INT, node->baseRank, TAG_BS_REPLY, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        node->alertRttTotal += MPI_Wtime() - node->alertSentTime;
        node->alertRttCount++;
        node->awaitingBaseReply = 0;
        node->messageCount++;
        node->byteCount += (long)sizeof(reply);

        if (reply[0] == BS_STATUS_SUGGEST) {
            fprintf(node->logFile, "[%s] Base station reply - forwarding to incoming EVs, nearest available node(s):", timestamp);
            for (int c = 0; c < reply[1] && c < MAX_CANDIDATES; c++) {
                int candidate = reply[2 + c];
                fprintf(node->logFile, " %d(%d,%d)", candidate,
                        candidate / node->config->cols, candidate % node->config->cols);
            }
            fprintf(node->logFile, "\n");
        } else {
            fprintf(node->logFile, "[%s] Base station reply - no nodes available nearby\n", timestamp);
        }
    } else if (status.MPI_TAG == TAG_TERMINATE) {
        int unused;
        MPI_Recv(&unused, 1, MPI_INT, node->baseRank, TAG_TERMINATE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        node->terminating = 1;
        node->messageCount++;
        node->byteCount += (long)sizeof(int);
        fprintf(node->logFile, "[%s] Termination received from base station - shutting down\n", timestamp);
    } else {
        /* Unknown tag - receive it anyway so the probe does not keep matching it */
        int byteCount = 0;
        MPI_Get_count(&status, MPI_BYTE, &byteCount);
        char *discard = malloc(byteCount > 0 ? (size_t)byteCount : 1);
        MPI_Recv(discard, byteCount, MPI_BYTE, node->baseRank, status.MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        free(discard);
        fprintf(node->logFile, "[%s] WARNING: unexpected tag %d from base station discarded\n", timestamp, status.MPI_TAG);
    }
}

/*
Final communication clean-up, called once the grid barrier has completed
Every neighbour has finished polling by now, so no further requests can arrive
*/
static void evnode_shutdown_comms(EVNode *node)
{
    MPI_Status status;
    int cancelled = 0;

    MPI_Cancel(&node->incomingRequest);
    MPI_Wait(&node->incomingRequest, &status);
    MPI_Test_cancelled(&status, &cancelled);
    if (!cancelled) {
        fprintf(node->logFile, "WARNING: a neighbour request matched during shutdown and was not answered\n");
    }

    MPI_Waitall(MAX_NEIGHBOURS, node->replyRequests, MPI_STATUSES_IGNORE);
    MPI_Wait(&node->alertRequest, MPI_STATUS_IGNORE);

    /* ACK carries this node's final statistics, so it is sent last */
    double ack[TERM_ACK_LEN];
    node->messageCount++;
    node->byteCount += (long)sizeof(ack);
    ack[ACK_ALERTS_SENT] = node->alertsSent;
    ack[ACK_POLLS] = node->pollCount;
    ack[ACK_POLL_TIME] = node->pollTimeTotal;
    ack[ACK_ALERT_RTT_COUNT] = node->alertRttCount;
    ack[ACK_ALERT_RTT_TIME] = node->alertRttTotal;
    ack[ACK_MESSAGES] = (double)node->messageCount;
    ack[ACK_BYTES] = (double)node->byteCount;
    ack[ACK_LOCAL_REDIRECTS] = node->localRedirects;
    MPI_Send(ack, TERM_ACK_LEN, MPI_DOUBLE, node->baseRank, TAG_TERM_ACK, MPI_COMM_WORLD);

    fprintf(node->logFile, "\nShutdown complete: %d cycles, %d polls, %d local redirects, %d alerts, %ld messages, %ld bytes\n",
            node->cycleCount, node->pollCount, node->localRedirects, node->alertsSent, node->messageCount, node->byteCount);
}

/*
Master thread: owns every MPI call (MPI_THREAD_FUNNELED)
Snapshots port availability mid-cycle, after the port threads have updated
*/
static void evnode_master_loop(EVNode *node, double startTime, int threadCount)
{
    double cycleSeconds = node->config->cycleMs / 1000.0;
    double nextSnapshot = startTime + cycleSeconds / 2.0;
    double nextPortUpdate = startTime;
    int finished = 0;

    while (!finished) {
        evnode_service_requests(node);
        evnode_check_base_messages(node);
        if (node->polling) evnode_check_poll(node);

        double now = omp_get_wtime();
        if (!node->terminating) {
            /* Fallback if the runtime only gave us one thread: the master updates the ports itself */
            if (threadCount == 1 && now >= nextPortUpdate) {
                for (int p = 0; p < node->config->portsPerNode; p++) {
                    port_simulate_cycle(&node->ports[p], node->config->busyProb);
                }
                nextPortUpdate += cycleSeconds;
            }

            if (now >= nextSnapshot) {
                nextSnapshot += cycleSeconds;
                int availability = evnode_count_available(node);
                evnode_record_cycle(node, availability);

                if (availability < node->config->threshold && !node->polling && !node->awaitingBaseReply) {
                    evnode_start_poll(node, availability);
                }
            }
        } else if (!node->polling) {
            /* Enter the grid barrier only once our own poll is finished, but keep answering neighbours */
            if (!node->barrierPosted) {
                MPI_Ibarrier(node->gridComm, &node->barrierRequest);
                node->barrierPosted = 1;
            }
            MPI_Test(&node->barrierRequest, &finished, MPI_STATUS_IGNORE);
        }

        if (!finished) wsn_sleep_us(COMM_LOOP_SLEEP_US);
    }

    evnode_shutdown_comms(node);

    #pragma omp atomic write
    node->running = 0;
}

/* Port threads: thread t (1..T-1) simulates ports t-1, t-1+(T-1), ... */
static void evnode_port_loop(EVNode *node, double startTime, int threadId, int threadCount)
{
    double cycleSeconds = node->config->cycleMs / 1000.0;
    double nextUpdate = startTime;

    while (evnode_is_running(node)) {
        if (omp_get_wtime() >= nextUpdate) {
            for (int p = threadId - 1; p < node->config->portsPerNode; p += threadCount - 1) {
                port_simulate_cycle(&node->ports[p], node->config->busyProb);
            }
            nextUpdate += cycleSeconds;
        }
        wsn_sleep_us(PORT_LOOP_SLEEP_US);
    }
}

static void evnode_free(EVNode *node)
{
    if (node->logFile != NULL) fclose(node->logFile);
    MPI_Type_free(&node->reportType);
}

/* Public entry point - runs one EV charging node until the base station terminates it */
int evnode_run(const SimConfig *config, MPI_Comm gridComm, int baseRank)
{
    EVNode *node = calloc(1, sizeof(EVNode));
    if (node == NULL) {
        fprintf(stderr, "EV node: out of memory\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    /* Other ranks would wait forever for this node, so an init failure aborts the whole job */
    if (evnode_init(node, config, gridComm, baseRank) != 0) {
        free(node);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    omp_set_dynamic(0);
    double startTime = omp_get_wtime();

    /* One master (MPI) thread + one thread per charging port */
    #pragma omp parallel num_threads(config->portsPerNode + 1) shared(node, startTime)
    {
        int threadId = omp_get_thread_num();
        int threadCount = omp_get_num_threads();

        if (threadId == 0) {
            evnode_master_loop(node, startTime, threadCount);
        } else {
            evnode_port_loop(node, startTime, threadId, threadCount);
        }
    }

    evnode_free(node);
    free(node);
    return 0;
}
