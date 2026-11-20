#include "stats.h"
#include <math.h>
#include <stdlib.h>

static int cmp_int64(const void *a, const void *b) {
    int64_t x = *(const int64_t *)a;
    int64_t y = *(const int64_t *)b;
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a;
    double y = *(const double *)b;
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

/* Nearest-rank percentile over an already-sorted int64 array. */
static double pct(const int64_t *sorted, size_t n, double p) {
    if (n == 0) return 0.0;
    double rank = ceil(p / 100.0 * (double)n);
    if (rank < 1.0) rank = 1.0;
    if (rank > (double)n) rank = (double)n;
    return (double)sorted[(size_t)rank - 1];
}

/* Nearest-rank percentile over an already-sorted double array. */
static double pct_double_sorted(const double *sorted, size_t n, double p) {
    if (n == 0) return 0.0;
    double rank = ceil(p / 100.0 * (double)n);
    if (rank < 1.0) rank = 1.0;
    if (rank > (double)n) rank = (double)n;
    return sorted[(size_t)rank - 1];
}

int qrtp_stats_compute(const int64_t *samples, size_t n,
                        qrtp_stats_t *out,
                        int64_t *sorted, double *diffs) {
    if (!samples || !out || n == 0) {
        return -1;
    }

    /* Copy + sort for percentile calculations. */
    for (size_t i = 0; i < n; i++) {
        sorted[i] = samples[i];
    }
    qsort(sorted, n, sizeof(int64_t), cmp_int64);

    int64_t min_ns = sorted[0];
    int64_t max_ns = sorted[n - 1];

    double sum = 0.0;
    for (size_t i = 0; i < n; i++) {
        double v = (double)samples[i];
        sum += v;
    }
    double mean_ns = sum / (double)n;

    double var = 0.0;
    for (size_t i = 0; i < n; i++) {
        double d = (double)samples[i] - mean_ns;
        var += d * d;
    }
    double stddev_ns = sqrt(var / (double)n);

    double p50_ns = pct(sorted, n, 50.0);
    double p95_ns = pct(sorted, n, 95.0);
    double p99_ns = pct(sorted, n, 99.0);

    /* Jitter: absolute consecutive differences in the original sample order.
     * diffs[] is caller scratch: fill it, take the max, then sort it in
     * place for the p99 (nearest-rank over the sorted diffs). */
    double jitter_max_ns = 0.0;
    size_t nd = (n > 1) ? (n - 1) : 0;
    for (size_t i = 0; i < nd; i++) {
        double d = fabs((double)samples[i + 1] - (double)samples[i]);
        diffs[i] = d;
        if (d > jitter_max_ns) jitter_max_ns = d;
    }
    double jitter_p99_ns = 0.0;
    if (nd > 0) {
        qsort(diffs, nd, sizeof(double), cmp_double);
        jitter_p99_ns = pct_double_sorted(diffs, nd, 99.0);
    }

    out->n = (int)n;
    out->min_us = min_ns / 1000.0;
    out->max_us = max_ns / 1000.0;
    out->mean_us = mean_ns / 1000.0;
    out->stddev_us = stddev_ns / 1000.0;
    out->p50_us = p50_ns / 1000.0;
    out->p95_us = p95_ns / 1000.0;
    out->p99_us = p99_ns / 1000.0;
    out->jitter_max_us = jitter_max_ns / 1000.0;
    out->jitter_p99_us = jitter_p99_ns / 1000.0;

    return 0;
}
