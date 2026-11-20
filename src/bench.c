/*
 * bench.c — host ping-pong benchmark for qnx-rt-probe.
 *
 * Two threads exchange a fixed 64-byte message over the transport:
 * the sender (high priority) records the round-trip time of every
 * iteration with CLOCK_MONOTONIC; the receiver (low priority) echoes
 * each message back. Warmup iterations are excluded from statistics.
 * Optional busy-loop load threads create CPU contention.
 *
 * Priority is attempted honestly: SCHED_FIFO with explicit priorities
 * (sender 20, receiver 10). Without privileges the pthread_create with
 * those attributes fails (EPERM) and we fall back to the default
 * scheduler, recording priority_elevated = 0. We never claim elevated
 * priority we did not get.
 */
#define _POSIX_C_SOURCE 200809L

#include "bench.h"
#include "transport.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct bench_ctx {
    qrtp_transport_t *transport;
    int total;             /* warmup + iterations */
    int warmup;
    int64_t *samples;      /* measured round-trips (ns), length iterations */
    atomic_int error;      /* set to 1 by any thread on failure */
    atomic_int stop_load;  /* set to 1 to stop the load threads */
} bench_ctx_t;

static int64_t timespec_ns(const struct timespec *ts) {
    return (int64_t)ts->tv_sec * 1000000000LL + (int64_t)ts->tv_nsec;
}

static void *sender_thread(void *arg) {
    bench_ctx_t *ctx = (bench_ctx_t *)arg;
    char msg[QRTP_MSG_LEN];
    char reply[QRTP_MSG_LEN];
    memset(msg, 0x51, sizeof(msg)); /* 'Q' */
    for (int i = 0; i < ctx->total; i++) {
        struct timespec t0, t1;
        if (clock_gettime(CLOCK_MONOTONIC, &t0) != 0) {
            atomic_store(&ctx->error, 1);
            return NULL;
        }
        if (ctx->transport->ops->send(ctx->transport, QRTP_EP_CLIENT, msg, sizeof(msg)) != 0) {
            atomic_store(&ctx->error, 1);
            return NULL;
        }
        if (ctx->transport->ops->recv(ctx->transport, QRTP_EP_CLIENT, reply, sizeof(reply)) != 0) {
            atomic_store(&ctx->error, 1);
            return NULL;
        }
        if (clock_gettime(CLOCK_MONOTONIC, &t1) != 0) {
            atomic_store(&ctx->error, 1);
            return NULL;
        }
        if (i >= ctx->warmup)
            ctx->samples[i - ctx->warmup] = timespec_ns(&t1) - timespec_ns(&t0);
    }
    return NULL;
}

static void *receiver_thread(void *arg) {
    bench_ctx_t *ctx = (bench_ctx_t *)arg;
    char msg[QRTP_MSG_LEN];
    char reply[QRTP_MSG_LEN];
    memset(reply, 0x52, sizeof(reply)); /* 'R' */
    for (int i = 0; i < ctx->total; i++) {
        if (ctx->transport->ops->recv(ctx->transport, QRTP_EP_SERVER, msg, sizeof(msg)) != 0) {
            atomic_store(&ctx->error, 1);
            return NULL;
        }
        if (ctx->transport->ops->send(ctx->transport, QRTP_EP_SERVER, reply, sizeof(reply)) != 0) {
            atomic_store(&ctx->error, 1);
            return NULL;
        }
    }
    return NULL;
}

static void *load_thread(void *arg) {
    bench_ctx_t *ctx = (bench_ctx_t *)arg;
    volatile unsigned long spin = 0;
    while (!atomic_load(&ctx->stop_load)) {
        /* Busy-loop: real CPU contention, no sleeps. */
        for (int i = 0; i < 1000; i++)
            spin += (unsigned long)i;
    }
    (void)spin;
    return NULL;
}

/* Create a thread with SCHED_FIFO at the given priority; on failure
 * (typically EPERM without privileges) fall back to default attributes.
 * Returns 0 on success; *elevated is set to 1 only if FIFO was applied. */
static int create_thread(pthread_t *th, void *(*fn)(void *), void *arg,
                         int fifo_prio, int *elevated) {
    pthread_attr_t attr;
    int used_fifo = 0;
    if (pthread_attr_init(&attr) == 0) {
        if (pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED) == 0 &&
            pthread_attr_setschedpolicy(&attr, SCHED_FIFO) == 0) {
            struct sched_param param;
            param.sched_priority = fifo_prio;
            if (pthread_attr_setschedparam(&attr, &param) == 0) {
                if (pthread_create(th, &attr, fn, arg) == 0)
                    used_fifo = 1;
            }
        }
        pthread_attr_destroy(&attr);
    }
    if (!used_fifo) {
        if (pthread_create(th, NULL, fn, arg) != 0)
            return -1;
    }
    if (elevated) *elevated = used_fifo;
    return 0;
}

int qrtp_bench_run(const qrtp_bench_opts_t *opts, qrtp_bench_result_t *res) {
    if (!opts || !res) return -1;
    if (opts->iterations <= 0 || opts->warmup < 0 || opts->load_threads < 0)
        return -1;
    memset(res, 0, sizeof(*res));
    res->error = 1;

    qrtp_transport_t transport;
    memset(&transport, 0, sizeof(transport));
    if (qrtp_transport_posix_create(&transport) != 0)
        return -1;

    bench_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.transport = &transport;
    ctx.total = opts->warmup + opts->iterations;
    ctx.warmup = opts->warmup;
    atomic_init(&ctx.error, 0);
    atomic_init(&ctx.stop_load, 0);

    ctx.samples = malloc(sizeof(int64_t) * (size_t)opts->iterations);
    if (!ctx.samples) {
        transport.ops->destroy(&transport);
        return -1;
    }

    int64_t *sorted = malloc(sizeof(int64_t) * (size_t)opts->iterations);
    double *diffs = NULL;
    if (opts->iterations > 1)
        diffs = malloc(sizeof(double) * (size_t)(opts->iterations - 1));
    if (!sorted || (opts->iterations > 1 && !diffs)) {
        free(ctx.samples);
        free(sorted);
        free(diffs);
        transport.ops->destroy(&transport);
        return -1;
    }

    int rc = -1;
    pthread_t sender, receiver;
    pthread_t *load_th = NULL;
    int load_started = 0;
    int elev_sender = 0, elev_receiver = 0;

    if (opts->load_threads > 0) {
        load_th = malloc(sizeof(pthread_t) * (size_t)opts->load_threads);
        if (!load_th) goto out;
        for (int i = 0; i < opts->load_threads; i++) {
            if (pthread_create(&load_th[i], NULL, load_thread, &ctx) != 0) {
                /* Best effort: fewer load threads is still a valid run. */
                break;
            }
            load_started++;
        }
    }

    if (create_thread(&receiver, receiver_thread, &ctx, 10, &elev_receiver) != 0)
        goto out;
    if (create_thread(&sender, sender_thread, &ctx, 20, &elev_sender) != 0) {
        pthread_join(receiver, NULL);
        goto out;
    }

    pthread_join(sender, NULL);
    pthread_join(receiver, NULL);
    atomic_store(&ctx.stop_load, 1);
    if (load_th) {
        for (int i = 0; i < load_started; i++)
            pthread_join(load_th[i], NULL);
    }

    if (atomic_load(&ctx.error) != 0)
        goto out;

    if (qrtp_stats_compute(ctx.samples, (size_t)opts->iterations,
                           &res->stats, sorted, diffs) != 0)
        goto out;

    res->priority_elevated = (elev_sender && elev_receiver) ? 1 : 0;
    res->error = 0;
    rc = 0;

out:
    free(load_th);
    free(ctx.samples);
    free(sorted);
    free(diffs);
    transport.ops->destroy(&transport);
    return rc;
}
