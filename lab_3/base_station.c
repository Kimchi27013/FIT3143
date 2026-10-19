/*
Base station (Lab 3, Task 2)

A single MPI process with a single communication thread. It:
  - listens for congestion reports without blocking (MPI_Iprobe loop, so the simulation clock keeps running)
  - answers from the reports it has already received - it never polls the nodes itself
  - applies the spec's update mechanism: if every neighbour of the reporting node reported within
    the predefined window -> "no nodes available nearby", otherwise suggest ALL nearest
    candidates two hops away that are not known to be congested
  - logs every alert and the key performance metrics to base_station.log
  - terminates the network and drains late alerts until every node has acknowledged (fix 5)

Clock note (Topic 9A): nodes may run on machines whose wall clocks differ, so the window is
measured with the base station's own receive time. The node timestamp is logged as information only.
*/

#include "wsn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

typedef struct {
    const SimConfig *config;
    int nodeCount;
    int baseRank;
    MPI_Datatype reportType;
    int reportTypeCreated;
    FILE *logFile;
    double startTime;
    int terminated;                         /* termination has been sent - alerts are now "late" */

    /* What the base station knows, indexed by node rank (times relative to startTime) */
    double *lastReportTime;                 /* when the node last sent an alert, -1 = never */
    int *knownAvail;                        /* last known availability from any report */
    double *knownAvailTime;                 /* when that availability was learned, -1 = never */

    /* Outgoing replies - at most one outstanding per node */
    int (*replyBuf)[BS_REPLY_LEN];
    MPI_Request *replyRequests;

    /* Termination handshake */
    int terminateBuf;
    MPI_Request *terminateRequests;
    double *terminateSentTime;
    double *terminateRtt;
    int *ackReceived;
    double (*ackStats)[TERM_ACK_LEN];
    int *alertsReceived;

    /* Metrics */
    long messagesSent;
    long messagesReceived;
    long bytesSent;
    long bytesReceived;
    int alertCount;
    int lateAlertCount;
    int suggestCount;
    int noneNearbyCount;
    double processingTimeTotal;
} BaseStation;

static int bs_init(BaseStation *bs, const SimConfig *config, int baseRank)
{
    memset(bs, 0, sizeof(BaseStation));
    bs->config = config;
    bs->baseRank = baseRank;
    bs->nodeCount = config->rows * config->cols;
    int nodeCount = bs->nodeCount;

    bs->lastReportTime = malloc(nodeCount * sizeof(double));
    bs->knownAvail = calloc(nodeCount, sizeof(int));
    bs->knownAvailTime = malloc(nodeCount * sizeof(double));
    bs->replyBuf = calloc(nodeCount, sizeof(*bs->replyBuf));
    bs->replyRequests = malloc(nodeCount * sizeof(MPI_Request));
    bs->terminateRequests = malloc(nodeCount * sizeof(MPI_Request));
    bs->terminateSentTime = calloc(nodeCount, sizeof(double));
    bs->terminateRtt = calloc(nodeCount, sizeof(double));
    bs->ackReceived = calloc(nodeCount, sizeof(int));
    bs->ackStats = calloc(nodeCount, sizeof(*bs->ackStats));
    bs->alertsReceived = calloc(nodeCount, sizeof(int));

    if (!bs->lastReportTime || !bs->knownAvail || !bs->knownAvailTime || !bs->replyBuf || !bs->replyRequests ||
        !bs->terminateRequests || !bs->terminateSentTime || !bs->terminateRtt || !bs->ackReceived ||
        !bs->ackStats || !bs->alertsReceived) {
        fprintf(stderr, "Base station: out of memory\n");
        return -1;
    }

    for (int i = 0; i < nodeCount; i++) {
        bs->lastReportTime[i] = -1.0;
        bs->knownAvailTime[i] = -1.0;
        bs->replyRequests[i] = MPI_REQUEST_NULL;
        bs->terminateRequests[i] = MPI_REQUEST_NULL;
    }

    bs->logFile = fopen("base_station.log", "w");
    if (bs->logFile == NULL) {
        fprintf(stderr, "Base station: could not open base_station.log\n");
        return -1;
    }

    wsn_create_report_type(&bs->reportType);
    bs->reportTypeCreated = 1;

    char timestamp[TIMESTAMP_LEN];
    wsn_timestamp(timestamp, sizeof(timestamp));
    fprintf(bs->logFile, "Base station log - started %s\n", timestamp);
    fprintf(bs->logFile, "Grid %d x %d (%d nodes), %d ports/node, cycle %d ms, threshold %d, busy prob %.2f, window %.1f s, run %.1f s\n\n",
            config->rows, config->cols, nodeCount, config->portsPerNode, config->cycleMs,
            config->threshold, config->busyProb, config->windowSeconds, config->simSeconds);
    return 0;
}

static void bs_free(BaseStation *bs)
{
    if (bs->logFile != NULL) fclose(bs->logFile);
    if (bs->reportTypeCreated) MPI_Type_free(&bs->reportType);
    free(bs->lastReportTime);
    free(bs->knownAvail);
    free(bs->knownAvailTime);
    free(bs->replyBuf);
    free(bs->replyRequests);
    free(bs->terminateRequests);
    free(bs->terminateSentTime);
    free(bs->terminateRtt);
    free(bs->ackReceived);
    free(bs->ackStats);
    free(bs->alertsReceived);
}

static int bs_is_recent(const BaseStation *bs, double eventTime, double now)
{
    return eventTime >= 0.0 && (now - eventTime) <= bs->config->windowSeconds;
}

static int bs_valid_rank(const BaseStation *bs, int rank)
{
    return rank >= 0 && rank < bs->nodeCount;
}

/*
Finds ALL nearest candidates for the reporting node
Candidates are the nodes two hops away (Manhattan distance 2) - e.g. Nodes 2, 4, 6 for Node 0 in a 3x3 grid -
excluding any node recently known to be below the threshold. Ties on Euclidean distance are all returned
Returns the number of candidates written to out
*/
static int bs_find_nearest_available(const BaseStation *bs, int source, double now, int *out)
{
    int cols = bs->config->cols;
    int sourceRow = source / cols;
    int sourceCol = source % cols;
    int bestDistance = INT_MAX;
    int count = 0;

    for (int row = 0; row < bs->config->rows; row++) {
        for (int col = 0; col < cols; col++) {
            int dRow = row - sourceRow;
            int dCol = col - sourceCol;
            if (abs(dRow) + abs(dCol) != 2) continue;

            int candidate = row * cols + col;
            if (bs_is_recent(bs, bs->knownAvailTime[candidate], now) &&
                bs->knownAvail[candidate] < bs->config->threshold) {
                continue;   /* known to be congested */
            }

            int squaredDistance = dRow * dRow + dCol * dCol;
            if (squaredDistance < bestDistance) {
                bestDistance = squaredDistance;
                count = 0;
            }
            if (squaredDistance == bestDistance && count < MAX_CANDIDATES) {
                out[count++] = candidate;
            }
        }
    }
    return count;
}

static void bs_log_report(const BaseStation *bs, const NodeReport *report, double now, const char *label)
{
    int cols = bs->config->cols;
    fprintf(bs->logFile, "[t=%.3fs] %s from node %d (%d,%d), detected %s\n",
            now, label, report->rank, report->coords[0], report->coords[1], report->timestamp);
    fprintf(bs->logFile, "  Reporting node availability: %d/%d | neighbour poll: %d msgs, %d bytes, %.6f s\n",
            report->availability, report->totalPorts, report->messagesExchanged,
            report->bytesExchanged, report->pollCommTime);
    fprintf(bs->logFile, "  Neighbours:");
    for (int i = 0; i < report->neighbourCount && i < MAX_NEIGHBOURS; i++) {
        int neighbour = report->neighbourRanks[i];
        fprintf(bs->logFile, " %d(%d,%d)=%d", neighbour, neighbour / cols, neighbour % cols, report->neighbourAvail[i]);
    }
    fprintf(bs->logFile, "\n");
}

/* Receives one alert, decides the answer and replies without blocking */
static void bs_handle_alert(BaseStation *bs, int source)
{
    NodeReport report;
    MPI_Recv(&report, 1, bs->reportType, source, TAG_ALERT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    double handleStart = MPI_Wtime();
    double now = handleStart - bs->startTime;
    bs->messagesReceived++;
    bs->bytesReceived += (long)sizeof(NodeReport);
    if (bs_valid_rank(bs, source)) bs->alertsReceived[source]++;

    if (bs->terminated) {
        /* Sent before the node saw TERMINATE - logged, no reply (the node is no longer waiting) */
        bs->lateAlertCount++;
        bs_log_report(bs, &report, now, "LATE ALERT (after termination, not answered)");
        return;
    }
    if (!bs_valid_rank(bs, source)) return;

    bs->alertCount++;
    char label[48];
    snprintf(label, sizeof(label), "ALERT #%d", bs->alertCount);
    bs_log_report(bs, &report, now, label);

    /* Spec update mechanism: did ALL neighbours report within the window? (checked before recording this report) */
    int allNeighboursReported = 1;
    for (int i = 0; i < report.neighbourCount && i < MAX_NEIGHBOURS; i++) {
        int neighbour = report.neighbourRanks[i];
        if (!bs_valid_rank(bs, neighbour) || !bs_is_recent(bs, bs->lastReportTime[neighbour], now)) {
            allNeighboursReported = 0;
        }
    }

    /* Record what this report tells us */
    bs->lastReportTime[source] = now;
    bs->knownAvail[source] = report.availability;
    bs->knownAvailTime[source] = now;
    for (int i = 0; i < report.neighbourCount && i < MAX_NEIGHBOURS; i++) {
        int neighbour = report.neighbourRanks[i];
        if (!bs_valid_rank(bs, neighbour)) continue;
        bs->knownAvail[neighbour] = report.neighbourAvail[i];
        bs->knownAvailTime[neighbour] = now;
    }

    /* The node only alerts again after this reply arrives, so the previous send is already complete */
    MPI_Wait(&bs->replyRequests[source], MPI_STATUS_IGNORE);
    int *reply = bs->replyBuf[source];
    memset(reply, 0, BS_REPLY_LEN * sizeof(int));

    if (allNeighboursReported) {
        reply[0] = BS_STATUS_NONE_NEARBY;
        reply[1] = 0;
    } else {
        int count = bs_find_nearest_available(bs, source, now, &reply[2]);
        reply[0] = count > 0 ? BS_STATUS_SUGGEST : BS_STATUS_NONE_NEARBY;
        reply[1] = count;
    }

    MPI_Isend(reply, BS_REPLY_LEN, MPI_INT, source, TAG_BS_REPLY, MPI_COMM_WORLD, &bs->replyRequests[source]);
    bs->messagesSent++;
    bs->bytesSent += (long)(BS_REPLY_LEN * sizeof(int));
    bs->processingTimeTotal += MPI_Wtime() - handleStart;

    fprintf(bs->logFile, "  All neighbours reported within %.1f s window: %s\n",
            bs->config->windowSeconds, allNeighboursReported ? "yes" : "no");
    if (reply[0] == BS_STATUS_SUGGEST) {
        bs->suggestCount++;
        fprintf(bs->logFile, "  Decision: suggest nearest available node(s):");
        for (int c = 0; c < reply[1]; c++) {
            int candidate = reply[2 + c];
            fprintf(bs->logFile, " %d(%d,%d)", candidate, candidate / bs->config->cols, candidate % bs->config->cols);
        }
        fprintf(bs->logFile, "\n\n");
    } else {
        bs->noneNearbyCount++;
        fprintf(bs->logFile, "  Decision: no nodes available nearby\n\n");
    }
}

static void bs_discard_message(BaseStation *bs, const MPI_Status *status)
{
    int byteCount = 0;
    MPI_Get_count(status, MPI_BYTE, &byteCount);
    char *discard = malloc(byteCount > 0 ? (size_t)byteCount : 1);
    MPI_Recv(discard, byteCount, MPI_BYTE, status->MPI_SOURCE, status->MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    free(discard);
    fprintf(bs->logFile, "WARNING: unexpected tag %d from rank %d discarded\n", status->MPI_TAG, status->MPI_SOURCE);
}

static void bs_send_termination(BaseStation *bs)
{
    bs->terminated = 1;
    fprintf(bs->logFile, "[t=%.3fs] Simulation time reached - sending TERMINATE to %d nodes\n\n",
            MPI_Wtime() - bs->startTime, bs->nodeCount);

    for (int i = 0; i < bs->nodeCount; i++) {
        bs->terminateSentTime[i] = MPI_Wtime();
        MPI_Isend(&bs->terminateBuf, 1, MPI_INT, i, TAG_TERMINATE, MPI_COMM_WORLD, &bs->terminateRequests[i]);
        bs->messagesSent++;
        bs->bytesSent += (long)sizeof(int);
    }
}

/*
Keeps receiving until every node has ACKed AND every alert each node says it sent has arrived
(tags can be matched out of order, so counting is what guarantees nothing is left unreceived)
*/
static void bs_drain_until_acknowledged(BaseStation *bs)
{
    int pending = 1;

    while (pending) {
        int arrived = 0;
        MPI_Status status;
        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &arrived, &status);

        if (arrived) {
            int source = status.MPI_SOURCE;
            if (status.MPI_TAG == TAG_ALERT) {
                bs_handle_alert(bs, source);
            } else if (status.MPI_TAG == TAG_TERM_ACK && bs_valid_rank(bs, source)) {
                MPI_Recv(bs->ackStats[source], TERM_ACK_LEN, MPI_DOUBLE, source, TAG_TERM_ACK,
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                bs->ackReceived[source] = 1;
                bs->terminateRtt[source] = MPI_Wtime() - bs->terminateSentTime[source];
                bs->messagesReceived++;
                bs->bytesReceived += (long)(TERM_ACK_LEN * sizeof(double));
            } else {
                bs_discard_message(bs, &status);
            }
        } else {
            wsn_sleep_us(COMM_LOOP_SLEEP_US);
        }

        pending = 0;
        for (int i = 0; i < bs->nodeCount; i++) {
            if (!bs->ackReceived[i] || bs->alertsReceived[i] < (int)bs->ackStats[i][ACK_ALERTS_SENT]) {
                pending = 1;
                break;
            }
        }
    }

    MPI_Waitall(bs->nodeCount, bs->terminateRequests, MPI_STATUSES_IGNORE);
    MPI_Waitall(bs->nodeCount, bs->replyRequests, MPI_STATUSES_IGNORE);
}

/* Writes the metrics summary to the log and appends one row per run to timing_results.csv (Task 3) */
static void bs_write_summary(BaseStation *bs)
{
    double totalTime = MPI_Wtime() - bs->startTime;
    double polls = 0, pollTime = 0, alertRtts = 0, alertRttTime = 0;
    double nodeMessages = 0, nodeBytes = 0, localRedirects = 0, terminateRttTotal = 0;

    for (int i = 0; i < bs->nodeCount; i++) {
        polls += bs->ackStats[i][ACK_POLLS];
        pollTime += bs->ackStats[i][ACK_POLL_TIME];
        alertRtts += bs->ackStats[i][ACK_ALERT_RTT_COUNT];
        alertRttTime += bs->ackStats[i][ACK_ALERT_RTT_TIME];
        nodeMessages += bs->ackStats[i][ACK_MESSAGES];
        nodeBytes += bs->ackStats[i][ACK_BYTES];
        localRedirects += bs->ackStats[i][ACK_LOCAL_REDIRECTS];
        terminateRttTotal += bs->terminateRtt[i];
    }

    double avgPoll = polls > 0 ? pollTime / polls : 0.0;
    double avgAlertRtt = alertRtts > 0 ? alertRttTime / alertRtts : 0.0;
    double avgProcessing = bs->alertCount > 0 ? bs->processingTimeTotal / bs->alertCount : 0.0;
    double avgTerminate = bs->nodeCount > 0 ? terminateRttTotal / bs->nodeCount : 0.0;
    long totalMessages = bs->messagesSent + bs->messagesReceived + (long)nodeMessages;

    FILE *log = bs->logFile;
    fprintf(log, "===== SIMULATION SUMMARY =====\n");
    fprintf(log, "Simulation time (s):                    %.3f\n", totalTime);
    fprintf(log, "Alerts detected:                        %d\n", bs->alertCount);
    fprintf(log, "Late alerts (after termination):        %d\n", bs->lateAlertCount);
    fprintf(log, "Replies - nodes suggested:              %d\n", bs->suggestCount);
    fprintf(log, "Replies - no nodes available nearby:    %d\n", bs->noneNearbyCount);
    fprintf(log, "Local redirects (handled by neighbours): %.0f\n", localRedirects);
    fprintf(log, "Neighbour polls:                        %.0f\n", polls);
    fprintf(log, "Base station messages sent / received:  %ld / %ld (%ld / %ld bytes)\n",
            bs->messagesSent, bs->messagesReceived, bs->bytesSent, bs->bytesReceived);
    fprintf(log, "Node-side messages (all nodes):         %.0f (%.0f bytes)\n", nodeMessages, nodeBytes);
    fprintf(log, "Total messages/events:                  %ld\n", totalMessages);
    fprintf(log, "\nAverage communication delay per message type:\n");
    fprintf(log, "  Neighbour availability poll (round trip): %.6f s\n", avgPoll);
    fprintf(log, "  Alert -> base station reply (round trip): %.6f s\n", avgAlertRtt);
    fprintf(log, "  Base station alert processing:            %.6f s\n", avgProcessing);
    fprintf(log, "  Terminate -> ACK (incl. grid barrier):    %.6f s\n", avgTerminate);

    FILE *existing = fopen("timing_results.csv", "r");
    int writeHeader = existing == NULL;
    if (existing != NULL) fclose(existing);

    FILE *csv = fopen("timing_results.csv", "a");
    if (csv == NULL) {
        fprintf(log, "WARNING: could not open timing_results.csv\n");
        return;
    }
    if (writeHeader) {
        fprintf(csv, "rows,cols,nodes,avg_poll_s,avg_alert_rtt_s,avg_bs_processing_s,avg_terminate_rtt_s,alerts,polls,total_messages\n");
    }
    fprintf(csv, "%d,%d,%d,%.9f,%.9f,%.9f,%.9f,%d,%.0f,%ld\n",
            bs->config->rows, bs->config->cols, bs->nodeCount, avgPoll, avgAlertRtt, avgProcessing,
            avgTerminate, bs->alertCount, polls, totalMessages);
    fclose(csv);
}

/* Public entry point - runs the base station for config->simSeconds, then shuts the network down */
int basestation_run(const SimConfig *config, int baseRank)
{
    BaseStation bs;
    /* Nodes would wait forever for a base station that failed to start, so abort the whole job */
    if (bs_init(&bs, config, baseRank) != 0) {
        bs_free(&bs);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    bs.startTime = MPI_Wtime();
    while (MPI_Wtime() - bs.startTime < config->simSeconds) {
        int arrived = 0;
        MPI_Status status;
        MPI_Iprobe(MPI_ANY_SOURCE, TAG_ALERT, MPI_COMM_WORLD, &arrived, &status);

        if (arrived) {
            bs_handle_alert(&bs, status.MPI_SOURCE);
        } else {
            wsn_sleep_us(COMM_LOOP_SLEEP_US);
        }
    }

    bs_send_termination(&bs);
    bs_drain_until_acknowledged(&bs);
    bs_write_summary(&bs);

    printf("Base station: %d alerts handled, %d late alerts, log written to base_station.log\n",
           bs.alertCount, bs.lateAlertCount);

    bs_free(&bs);
    return 0;
}
