/*
 * qnx_demo.c — QNX Neutrino guest demo for qnx-rt-probe.
 *
 * Compiles ONLY under qcc (QNX SDP 8.0). NOT compiled by the host build.
 * First qcc compile VERIFIED 20 Nov 2026 (M1); thresholds re-calibrated
 * for the guest in M3 (see QRTP_GUEST_*_THRESHOLD_US below).
 *
 * Mirrors the host benchmark (bench.c) with the native transport:
 * a high-priority sender thread (SCHED_FIFO, prio 20) and a low-priority
 * receiver thread (SCHED_FIFO, prio 10) exchange a 64-byte message via
 * MsgSend/MsgReceive, under optional CPU load. Prints one JSON line per
 * run to stdout (serial console in QEMU), using the same field names as
 * the host report so the host `qrtp report` reader can consume it.
 *
 * Build once the SDP is installed (from the repo root):
 *   source ~/qnx/qnx800/qnxsdp-env.sh
 *   qcc -Vgcc_ntox86_64 -O2 -o qnx_demo src/qnx_demo.c src/transport_qnx.c src/stats.c -lm
 */
#ifdef __QNX__

#define _POSIX_C_SOURCE 200809L

#include "stats.h"
#include "transport.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Guest-calibrated PASS thresholds (M3, 20 Nov 2026).
 * Rule (docs/CODELAB.md section 6): 2x the worst p50 / worst p99 seen
 * across all guest runs, rounded up to a round number.
 *   p50 across 6 guest runs (2000 iters, loads 0-4): 20.4-20.9 us,
 *     worst 20.9 -> 2x = 41.8 -> 50 us.
 *   p99 across the same runs: 31.5-47.4 us, worst 47.4 -> 2x = 94.8
 *     -> 100 us.
 * Every guest run so far passes these with ~2x margin; a 2.4x rise in
 * p50 or a 2.1x rise in p99 fails the build. The host defaults
 * (100/500 us, "host-default") were placeholders and are NOT used here.
 */
#define QRTP_GUEST_P50_THRESHOLD_US 50.0
#define QRTP_GUEST_P99_THRESHOLD_US 100.0

typedef struct demo_ctx {
    qrtp_transport_t *transport;
    int total;
    int warmup;
    int64_t *samples;
    int error;
    volatile int stop_load;
} demo_ctx_t;

static int64_t timespec_ns(const struct timespec *ts) {
    return (int64_t)ts->tv_sec * 1000000000LL + (int64_t)ts->tv_nsec;
}

static void *sender_thread(void *arg) {
    demo_ctx_t *ctx = (demo_ctx_t *)arg;
    char msg[QRTP_MSG_LEN];
    char reply[QRTP_MSG_LEN];
    memset(msg, 0x51, sizeof(msg));
    for (int i = 0; i < ctx->total; i++) {
        struct timespec t0, t1;
        if (clock_gettime(CLOCK_MONOTONIC, &t0) != 0) { ctx->error = 1; return NULL; }
        if (ctx->transport->ops->send(ctx->transport, QRTP_EP_CLIENT, msg, sizeof(msg)) != 0) {
            ctx->error = 1;
            return NULL;
        }
        if (ctx->transport->ops->recv(ctx->transport, QRTP_EP_CLIENT, reply, sizeof(reply)) != 0) {
            ctx->error = 1;
            return NULL;
        }
        if (clock_gettime(CLOCK_MONOTONIC, &t1) != 0) { ctx->error = 1; return NULL; }
        if (i >= ctx->warmup)
            ctx->samples[i - ctx->warmup] = timespec_ns(&t1) - timespec_ns(&t0);
    }
    return NULL;
}

static void *receiver_thread(void *arg) {
    demo_ctx_t *ctx = (demo_ctx_t *)arg;
    char msg[QRTP_MSG_LEN];
    char reply[QRTP_MSG_LEN];
    memset(reply, 0x52, sizeof(reply));
    for (int i = 0; i < ctx->total; i++) {
        if (ctx->transport->ops->recv(ctx->transport, QRTP_EP_SERVER, msg, sizeof(msg)) != 0) {
            ctx->error = 1;
            return NULL;
        }
        if (ctx->transport->ops->send(ctx->transport, QRTP_EP_SERVER, reply, sizeof(reply)) != 0) {
            ctx->error = 1;
            return NULL;
        }
    }
    return NULL;
}

static void *load_thread(void *arg) {
    demo_ctx_t *ctx = (demo_ctx_t *)arg;
    volatile unsigned long spin = 0;
    while (!ctx->stop_load) {
        for (int i = 0; i < 1000; i++)
            spin += (unsigned long)i;
    }
    (void)spin;
    return NULL;
}

static int make_fifo_thread(pthread_t *th, void *(*fn)(void *), void *arg, int prio) {
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) return -1;
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    struct sched_param param;
    param.sched_priority = prio;
    pthread_attr_setschedparam(&attr, &param);
    int rc = pthread_create(th, &attr, fn, arg);
    pthread_attr_destroy(&attr);
    return rc;
}

static void print_usage(void) {
    fprintf(stderr, "Usage: qnx_demo [--iterations N] [--warmup W] [--load N]\n");
}

int main(int argc, char **argv) {
    int iterations = 10000, warmup = 500, load = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) iterations = atoi(argv[++i]);
        else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) warmup = atoi(argv[++i]);
        else if (strcmp(argv[i], "--load") == 0 && i + 1 < argc) load = atoi(argv[++i]);
        else { print_usage(); return 1; }
    }
    if (iterations <= 0 || warmup < 0 || load < 0) { print_usage(); return 1; }

    qrtp_transport_t transport;
    memset(&transport, 0, sizeof(transport));
    if (qrtp_transport_qnx_create(&transport) != 0) {
        fprintf(stderr, "qnx_demo: transport create failed\n");
        return 1;
    }

    demo_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &transport;
    ctx.total = warmup + iterations;
    ctx.warmup = warmup;
    ctx.samples = malloc(sizeof(int64_t) * (size_t)iterations);
    int64_t *sorted = malloc(sizeof(int64_t) * (size_t)iterations);
    double *diffs = iterations > 1 ? malloc(sizeof(double) * (size_t)(iterations - 1)) : NULL;
    if (!ctx.samples || !sorted || (iterations > 1 && !diffs)) {
        fprintf(stderr, "qnx_demo: out of memory\n");
        return 1;
    }

    pthread_t *load_th = NULL;
    if (load > 0) {
        load_th = malloc(sizeof(pthread_t) * (size_t)load);
        for (int i = 0; load_th && i < load; i++)
            pthread_create(&load_th[i], NULL, load_thread, &ctx);
    }

    pthread_t sender, receiver;
    if (make_fifo_thread(&receiver, receiver_thread, &ctx, 10) != 0 ||
        make_fifo_thread(&sender, sender_thread, &ctx, 20) != 0) {
        fprintf(stderr, "qnx_demo: thread create failed: %s\n", strerror(errno));
        return 1;
    }
    pthread_join(sender, NULL);
    pthread_join(receiver, NULL);
    ctx.stop_load = 1;
    if (load_th) {
        for (int i = 0; i < load; i++)
            pthread_join(load_th[i], NULL);
    }

    int rc = 1;
    if (ctx.error) {
        fprintf(stderr, "qnx_demo: benchmark failed\n");
        goto out;
    }
    qrtp_stats_t st;
    if (qrtp_stats_compute(ctx.samples, (size_t)iterations, &st, sorted, diffs) != 0) {
        fprintf(stderr, "qnx_demo: stats failed\n");
        goto out;
    }
    /* One JSON line, same field names as the host report. */
    printf("{\"tool\":\"qnx-rt-probe\",\"tool_version\":\"0.1.0\","
           "\"transport\":\"qnx-msgsend\",\"host\":false,\"qnx\":true,"
           "\"iterations\":%d,\"warmup\":%d,\"load_threads\":%d,"
           "\"priority_elevated\":true,"
           "\"thresholds_us\":{\"p50\":%.1f,\"p99\":%.1f,\"source\":\"guest-calibrated\"},"
           "\"stats\":{\"count\":%d,\"min_us\":%.1f,\"max_us\":%.1f,"
           "\"mean_us\":%.1f,\"stddev_us\":%.1f,\"p50_us\":%.1f,"
           "\"p95_us\":%.1f,\"p99_us\":%.1f,\"jitter_max_us\":%.1f,"
           "\"jitter_p99_us\":%.1f},"
           "\"verdict\":\"%s\"}\n",
           iterations, warmup, load,
           QRTP_GUEST_P50_THRESHOLD_US, QRTP_GUEST_P99_THRESHOLD_US,
           st.n, st.min_us, st.max_us, st.mean_us, st.stddev_us,
           st.p50_us, st.p95_us, st.p99_us, st.jitter_max_us, st.jitter_p99_us,
           (st.p50_us <= QRTP_GUEST_P50_THRESHOLD_US &&
            st.p99_us <= QRTP_GUEST_P99_THRESHOLD_US) ? "PASS" : "FAIL");
    fflush(stdout);
    rc = 0;
out:
    free(load_th);
    free(ctx.samples);
    free(sorted);
    free(diffs);
    transport.ops->destroy(&transport);
    return rc;
}

#else /* !__QNX__ */
#error "qnx_demo.c compiles only under qcc (QNX SDP 8.0) — do not add it to the host build."
#endif
