#ifndef QRTP_STATS_H
#define QRTP_STATS_H

#include <stddef.h>
#include <stdint.h>

/* Statistics computed from a sample array (nanoseconds).
 * Results are stored as double microseconds. */
typedef struct qrtp_stats {
    int n;
    double min_us;
    double max_us;
    double mean_us;
    double stddev_us;
    double p50_us;
    double p95_us;
    double p99_us;
    double jitter_max_us;
    double jitter_p99_us;
} qrtp_stats_t;

/* Compute statistics from `samples` (nanoseconds), `n` samples.
 * Returns 0 on success, -1 on error (e.g. n == 0).
 * The `sorted` and `diffs` buffers are scratch space (caller-allocated). */
int qrtp_stats_compute(const int64_t *samples, size_t n,
                        qrtp_stats_t *out,
                        int64_t *sorted, double *diffs);

#endif /* QRTP_STATS_H */
