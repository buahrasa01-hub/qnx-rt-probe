/* test_stats.c — deterministic unit test for src/stats.c.
 *
 * Hand-computed expectations; no randomness, no I/O beyond stdout.
 * Exit 0 when every check passes, 1 otherwise.
 */
#include "stats.h"

#include <math.h>
#include <stdio.h>

static int failures = 0;

static void check_double(const char *name, double got, double want) {
    if (fabs(got - want) > 0.01) {
        printf("FAIL %s: got %.4f, want %.4f\n", name, got, want);
        failures++;
    } else {
        printf("ok   %s: %.4f\n", name, got);
    }
}

int main(void) {
    /* Dataset 1: 1..100 us (as ns), consecutive diffs all 1 us. */
    int64_t s1[100];
    for (int i = 0; i < 100; i++)
        s1[i] = (int64_t)(i + 1) * 1000;
    int64_t sorted[100];
    double diffs[99];
    qrtp_stats_t st;
    if (qrtp_stats_compute(s1, 100, &st, sorted, diffs) != 0) {
        printf("FAIL compute returned error for n=100\n");
        return 1;
    }
    check_double("count", (double)st.n, 100.0);
    check_double("min_us", st.min_us, 1.0);
    check_double("max_us", st.max_us, 100.0);
    check_double("mean_us", st.mean_us, 50.5);
    check_double("stddev_us", st.stddev_us, 28.87);
    check_double("p50_us", st.p50_us, 50.0);
    check_double("p95_us", st.p95_us, 95.0);
    check_double("p99_us", st.p99_us, 99.0);
    check_double("jitter_max_us", st.jitter_max_us, 1.0);
    check_double("jitter_p99_us", st.jitter_p99_us, 1.0);

    /* Dataset 2: one outlier — p99 and jitter must see it. */
    int64_t s2[5] = { 1000, 1000, 1000, 1000, 100000 };
    int64_t sorted2[5];
    double diffs2[4];
    qrtp_stats_t st2;
    if (qrtp_stats_compute(s2, 5, &st2, sorted2, diffs2) != 0) {
        printf("FAIL compute returned error for n=5\n");
        return 1;
    }
    check_double("outlier p50_us", st2.p50_us, 1.0);
    check_double("outlier p99_us", st2.p99_us, 100.0);
    check_double("outlier jitter_max_us", st2.jitter_max_us, 99.0);
    check_double("outlier jitter_p99_us", st2.jitter_p99_us, 99.0);

    /* Edge cases. */
    if (qrtp_stats_compute(NULL, 0, &st, sorted, diffs) == 0) {
        printf("FAIL n=0 should return -1\n");
        failures++;
    } else {
        printf("ok   n=0 returns -1\n");
    }
    int64_t s3[1] = { 5000 };
    int64_t sorted3[1];
    qrtp_stats_t st3;
    if (qrtp_stats_compute(s3, 1, &st3, sorted3, NULL) != 0) {
        printf("FAIL n=1 should succeed\n");
        failures++;
    } else {
        check_double("n=1 p50_us", st3.p50_us, 5.0);
        check_double("n=1 jitter_max_us", st3.jitter_max_us, 0.0);
        check_double("n=1 jitter_p99_us", st3.jitter_p99_us, 0.0);
    }

    if (failures == 0) {
        printf("test_stats: ALL PASS\n");
        return 0;
    }
    printf("test_stats: %d FAILURES\n", failures);
    return 1;
}
