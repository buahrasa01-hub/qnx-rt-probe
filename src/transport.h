#ifndef QRTP_TRANSPORT_H
#define QRTP_TRANSPORT_H

#include <stddef.h>

/* Fixed ping-pong message size (bytes). */
#define QRTP_MSG_LEN 64

/* Endpoints of a channel: 0 = server/receiver side, 1 = client/sender side.
 * The bench's sender thread uses endpoint 1, the receiver thread endpoint 0.
 */
#define QRTP_EP_SERVER 0
#define QRTP_EP_CLIENT 1

typedef struct qrtp_transport qrtp_transport_t;

typedef struct qrtp_transport_ops {
    int (*create)(qrtp_transport_t *t);
    int (*destroy)(qrtp_transport_t *t);
    /* Send exactly len bytes from endpoint ep. Returns 0 or -1. */
    int (*send)(qrtp_transport_t *t, int ep, const void *buf, size_t len);
    /* Receive exactly len bytes at endpoint ep (blocks). Returns 0 or -1. */
    int (*recv)(qrtp_transport_t *t, int ep, void *buf, size_t len);
} qrtp_transport_ops_t;

struct qrtp_transport {
    const qrtp_transport_ops_t *ops;
    void *priv; /* implementation-private state */
};

/* POSIX host transport constructor (socketpair-based).
 * Implemented in transport_posix.c. */
int qrtp_transport_posix_create(qrtp_transport_t *t);

#ifdef __QNX__
/* QNX Neutrino transport constructor (MsgSend/MsgReceive-based).
 * Implemented in transport_qnx.c. Compiles only under qcc. */
int qrtp_transport_qnx_create(qrtp_transport_t *t);
#endif

#endif /* QRTP_TRANSPORT_H */
