/*
 * report.c — JSON + human-readable reporting for qnx-rt-probe.
 *
 * The writer emits one fixed-shape JSON document (see README for the
 * schema). The reader is deliberately minimal: it scans for the exact
 * keys our writer emits and parses their values. It is NOT a general
 * JSON parser — it only re-reads reports this tool produced, and it
 * fails loudly on anything else.
 */
#define _POSIX_C_SOURCE 200809L

#include "report.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define QRTP_TOOL "qnx-rt-probe"
#define QRTP_VERSION "0.1.0"

const char *qrtp_verdict(double p50_us, double p99_us,
                         double p50_threshold_us, double p99_threshold_us) {
    return (p50_us <= p50_threshold_us && p99_us <= p99_threshold_us)
               ? "PASS"
               : "FAIL";
}

static void iso8601_utc(char *buf, size_t len) {
    time_t now = time(NULL);
    struct tm tmv;
    gmtime_r(&now, &tmv);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

int qrtp_report_write(const char *path, const qrtp_bench_opts_t *opts,
                      const qrtp_bench_result_t *res, const char *verdict) {
    if (!path || !opts || !res || !verdict) return -1;
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    char ts[32];
    iso8601_utc(ts, sizeof(ts));
    const qrtp_stats_t *s = &res->stats;
    fprintf(f, "{\n");
    fprintf(f, "  \"tool\": \"%s\",\n", QRTP_TOOL);
    fprintf(f, "  \"tool_version\": \"%s\",\n", QRTP_VERSION);
    fprintf(f, "  \"transport\": \"posix-socketpair\",\n");
    fprintf(f, "  \"host\": true,\n");
    fprintf(f, "  \"qnx\": false,\n");
    fprintf(f, "  \"iterations\": %d,\n", opts->iterations);
    fprintf(f, "  \"warmup\": %d,\n", opts->warmup);
    fprintf(f, "  \"load_threads\": %d,\n", opts->load_threads);
    fprintf(f, "  \"priority_elevated\": %s,\n", res->priority_elevated ? "true" : "false");
    fprintf(f, "  \"thresholds_us\": {\"p50\": %.1f, \"p99\": %.1f, \"source\": \"host-default\"},\n",
            opts->p50_threshold_us, opts->p99_threshold_us);
    fprintf(f, "  \"stats\": {\n");
    fprintf(f, "    \"count\": %d,\n", s->n);
    fprintf(f, "    \"min_us\": %.1f,\n", s->min_us);
    fprintf(f, "    \"max_us\": %.1f,\n", s->max_us);
    fprintf(f, "    \"mean_us\": %.1f,\n", s->mean_us);
    fprintf(f, "    \"stddev_us\": %.1f,\n", s->stddev_us);
    fprintf(f, "    \"p50_us\": %.1f,\n", s->p50_us);
    fprintf(f, "    \"p95_us\": %.1f,\n", s->p95_us);
    fprintf(f, "    \"p99_us\": %.1f,\n", s->p99_us);
    fprintf(f, "    \"jitter_max_us\": %.1f,\n", s->jitter_max_us);
    fprintf(f, "    \"jitter_p99_us\": %.1f\n", s->jitter_p99_us);
    fprintf(f, "  },\n");
    fprintf(f, "  \"verdict\": \"%s\",\n", verdict);
    fprintf(f, "  \"generated_at\": \"%s\"\n", ts);
    fprintf(f, "}\n");
    if (fclose(f) != 0) return -1;
    return 0;
}

void qrtp_report_print(const qrtp_bench_opts_t *opts,
                       const qrtp_bench_result_t *res, const char *verdict) {
    const qrtp_stats_t *s = &res->stats;
    printf("%s %s — host benchmark (posix-socketpair)\n", QRTP_TOOL, QRTP_VERSION);
    printf("iterations: %d (warmup %d excluded)   load threads: %d\n",
           opts->iterations, opts->warmup, opts->load_threads);
    printf("priority elevated: %s", res->priority_elevated ? "yes" : "no");
    if (!res->priority_elevated)
        printf(" (SCHED_FIFO unavailable — unprivileged user; ran under SCHED_OTHER)");
    printf("\n");
    printf("thresholds (host-default): p50 <= %.1f us, p99 <= %.1f us\n",
           opts->p50_threshold_us, opts->p99_threshold_us);
    printf("  count          %d\n", s->n);
    printf("  min            %.1f us\n", s->min_us);
    printf("  max            %.1f us\n", s->max_us);
    printf("  mean           %.1f us\n", s->mean_us);
    printf("  stddev         %.1f us\n", s->stddev_us);
    printf("  p50            %.1f us\n", s->p50_us);
    printf("  p95            %.1f us\n", s->p95_us);
    printf("  p99            %.1f us\n", s->p99_us);
    printf("  jitter max     %.1f us\n", s->jitter_max_us);
    printf("  jitter p99     %.1f us\n", s->jitter_p99_us);
    printf("verdict: %s\n", verdict);
}

/* --- minimal reader (own reports only) --- */

static const char *find_key(const char *json, const char *key) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return NULL;
    p = strchr(p + strlen(pat), ':');
    return p ? p + 1 : NULL;
}

static int read_double(const char *json, const char *key, double *out) {
    const char *p = find_key(json, key);
    if (!p) return -1;
    char *end = NULL;
    double v = strtod(p, &end);
    if (end == p) return -1;
    *out = v;
    return 0;
}

static int read_int(const char *json, const char *key, int *out) {
    double v;
    if (read_double(json, key, &v) != 0) return -1;
    *out = (int)v;
    return 0;
}

static int read_bool(const char *json, const char *key, int *out) {
    const char *p = find_key(json, key);
    if (!p) return -1;
    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
    if (strncmp(p, "true", 4) == 0) { *out = 1; return 0; }
    if (strncmp(p, "false", 5) == 0) { *out = 0; return 0; }
    return -1;
}

static int read_string(const char *json, const char *key,
                       char *out, size_t len) {
    const char *p = find_key(json, key);
    if (!p) return -1;
    p = strchr(p, '"');
    if (!p) return -1;
    p++;
    const char *e = strchr(p, '"');
    if (!e || (size_t)(e - p) >= len) return -1;
    memcpy(out, p, (size_t)(e - p));
    out[e - p] = '\0';
    return 0;
}

int qrtp_report_read(const char *path, qrtp_bench_opts_t *opts,
                     qrtp_bench_result_t *res,
                     char *verdict_out, size_t verdict_len) {
    if (!path || !opts || !res || !verdict_out) return -1;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size <= 0 || size > 1024 * 1024) { fclose(f); return -1; }
    fseek(f, 0, SEEK_SET);
    char *json = malloc((size_t)size + 1);
    if (!json) { fclose(f); return -1; }
    if (fread(json, 1, (size_t)size, f) != (size_t)size) {
        free(json);
        fclose(f);
        return -1;
    }
    json[size] = '\0';
    fclose(f);

    /* The "stats" object repeats some key names (count, p50_us...); the
     * reader looks for the first occurrence, which for count/min/max is
     * inside stats because the top level has no such keys. thresholds
     * live under "thresholds_us": p50/p99 there are the thresholds. */
    memset(opts, 0, sizeof(*opts));
    memset(res, 0, sizeof(*res));
    int rc = -1;
    char tool[64];
    if (read_string(json, "tool", tool, sizeof(tool)) != 0 ||
        strcmp(tool, QRTP_TOOL) != 0)
        goto done;
    if (read_int(json, "iterations", &opts->iterations) != 0 ||
        read_int(json, "warmup", &opts->warmup) != 0 ||
        read_int(json, "load_threads", &opts->load_threads) != 0 ||
        read_bool(json, "priority_elevated", &res->priority_elevated) != 0)
        goto done;
    /* thresholds: keys "p50"/"p99" appear only inside thresholds_us. */
    if (read_double(json, "p50", &opts->p50_threshold_us) != 0 ||
        read_double(json, "p99", &opts->p99_threshold_us) != 0)
        goto done;
    if (read_int(json, "count", &res->stats.n) != 0 ||
        read_double(json, "min_us", &res->stats.min_us) != 0 ||
        read_double(json, "max_us", &res->stats.max_us) != 0 ||
        read_double(json, "mean_us", &res->stats.mean_us) != 0 ||
        read_double(json, "stddev_us", &res->stats.stddev_us) != 0 ||
        read_double(json, "p50_us", &res->stats.p50_us) != 0 ||
        read_double(json, "p95_us", &res->stats.p95_us) != 0 ||
        read_double(json, "p99_us", &res->stats.p99_us) != 0 ||
        read_double(json, "jitter_max_us", &res->stats.jitter_max_us) != 0 ||
        read_double(json, "jitter_p99_us", &res->stats.jitter_p99_us) != 0)
        goto done;
    if (read_string(json, "verdict", verdict_out, verdict_len) != 0)
        goto done;
    if (strcmp(verdict_out, "PASS") != 0 && strcmp(verdict_out, "FAIL") != 0)
        goto done;
    rc = 0;
done:
    free(json);
    return rc;
}
