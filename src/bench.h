#ifndef QRTP_BENCH_H
#define QRTP_BENCH_H

#include "stats.h"

typedef struct qrtp_bench_opts {
    int iterations;        /* measured iterations (warmup excluded) */
    int warmup;            /* warmup iterations, excluded from stats */
    int load_threads;      /* busy-loop CPU-load threads (0 = none) */
    double p50_threshold_us;
    double p99_threshold_us;
    char threshold_source[32]; /* e.g. "host-default", "guest-calibrated" */
} qrtp_bench_opts_t;

typedef struct qrtp_bench_result {
    qrtp_stats_t stats;
    int priority_elevated; /* 1 only if SCHED_FIFO was actually applied */
    int qnx;               /* 1 if this report came from the QNX guest */
    int error;             /* 0 on success */
} qrtp_bench_result_t;

/* Run the host ping-pong benchmark. Returns 0 on success, -1 on error
 * (opts invalid, transport failure, or a thread error). On success res
 * holds the statistics and whether real-time priority was achieved. */
int qrtp_bench_run(const qrtp_bench_opts_t *opts, qrtp_bench_result_t *res);

#endif /* QRTP_BENCH_H */
