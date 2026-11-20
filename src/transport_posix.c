/*
 * transport_posix.c — POSIX host transport for qnx-rt-probe.
 *
 * One Unix socketpair (AF_UNIX, SOCK_DGRAM) backs the channel. SOCK_DGRAM
 * (not SOCK_STREAM) is deliberate: datagram boundaries preserve the fixed
 * 64-byte messages, so one send is always exactly one recv — the same
 * message-at-a-time semantics as QNX MsgSend/MsgReceive, which keeps the
 * host benchmark a faithful stand-in for the QNX guest measurement.
 *
 * Endpoint 0 owns fd[0], endpoint 1 owns fd[1]. The bench sender uses
 * endpoint 1, the receiver endpoint 0.
 */
#include "transport.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct posix_priv {
    int fd[2];
} posix_priv_t;

static int posix_create(qrtp_transport_t *t) {
    posix_priv_t *p = t->priv;
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, p->fd) != 0)
        return -1;
    return 0;
}

static int posix_destroy(qrtp_transport_t *t) {
    posix_priv_t *p = t->priv;
    int rc = 0;
    if (p->fd[0] >= 0 && close(p->fd[0]) != 0) rc = -1;
    if (p->fd[1] >= 0 && close(p->fd[1]) != 0) rc = -1;
    p->fd[0] = p->fd[1] = -1;
    return rc;
}

static int posix_send(qrtp_transport_t *t, int ep, const void *buf, size_t len) {
    posix_priv_t *p = t->priv;
    if (ep != 0 && ep != 1) return -1;
    const char *ptr = (const char *)buf;
    size_t left = len;
    /* SOCK_DGRAM delivers the whole datagram in one send; loop only for
     * EINTR safety. A short send on a datagram socket cannot happen for
     * our 64-byte messages, but guard anyway. */
    while (left > 0) {
        ssize_t n = send(p->fd[ep], ptr, left, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        ptr += n;
        left -= (size_t)n;
    }
    return 0;
}

static int posix_recv(qrtp_transport_t *t, int ep, void *buf, size_t len) {
    posix_priv_t *p = t->priv;
    if (ep != 0 && ep != 1) return -1;
    char *ptr = (char *)buf;
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(p->fd[ep], ptr + got, len - got, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1; /* peer closed */
        got += (size_t)n;
    }
    return 0;
}

static const qrtp_transport_ops_t posix_ops = {
    .create = posix_create,
    .destroy = posix_destroy,
    .send = posix_send,
    .recv = posix_recv,
};

int qrtp_transport_posix_create(qrtp_transport_t *t) {
    static posix_priv_t priv = { { -1, -1 } };
    if (!t) return -1;
    memset(&priv, 0, sizeof(priv));
    priv.fd[0] = priv.fd[1] = -1;
    t->ops = &posix_ops;
    t->priv = &priv;
    return t->ops->create(t);
}
