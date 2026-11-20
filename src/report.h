#ifndef QRTP_REPORT_H
#define QRTP_REPORT_H

#include "bench.h"
#include <stddef.h>

/* Verdict from the measured percentiles vs thresholds. */
const char *qrtp_verdict(double p50_us, double p99_us,
                         double p50_threshold_us, double p99_threshold_us);

/* Write the JSON report to path. Returns 0 on success, -1 on error. */
int qrtp_report_write(const char *path, const qrtp_bench_opts_t *opts,
                      const qrtp_bench_result_t *res, const char *verdict);

/* Print the human-readable table to stdout. */
void qrtp_report_print(const qrtp_bench_opts_t *opts,
                       const qrtp_bench_result_t *res, const char *verdict);

/* Minimal reader for reports our own writer produced. Validates that the
 * required keys exist; returns 0 on success, -1 on missing/malformed data.
 * verdict_out gets "PASS" or "FAIL". */
int qrtp_report_read(const char *path, qrtp_bench_opts_t *opts,
                     qrtp_bench_result_t *res,
                     char *verdict_out, size_t verdict_len);

#endif /* QRTP_REPORT_H */
