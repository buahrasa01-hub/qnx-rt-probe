/*
 * transport_qnx.c — QNX Neutrino transport for qnx-rt-probe.
 *
 * Compiles ONLY under qcc (QNX SDP 8.0). NOT compiled by the host build;
 * the #else branch below fails loudly if this file is ever added to a
 * host compile by mistake.
 *
 * API usage is written from the public QNX Neutrino documentation and is
 * UNVERIFIED until the first qcc compile (M1).
 *
 * Mapping onto the symmetric send/recv interface:
 *   - One channel is created (ChannelCreate) and the process connects to
 *     its own channel (ConnectAttach to ND_LOCAL_NODE) — valid for this
 *     same-process demo; a real multi-process setup would attach to the
 *     server's channel id instead.
 *   - Server side (endpoint 0): recv = MsgReceive (stores rcvid),
 *     send = MsgReply(rcvid, ...).
 *   - Client side (endpoint 1): send = MsgSend, which blocks until the
 *     server replies; the reply bytes are kept in an internal buffer and
 *     the following recv copies them out immediately. This preserves the
 *     bench's round-trip timing: t0 before send, t1 after recv.
 */
#ifdef __QNX__

#include "transport.h"

#include <errno.h>
#include <string.h>
#include <sys/neutrino.h>
#include <sys/netmgr.h>

typedef struct qnx_priv {
    int chid;
    int coid;
    int rcvid;             /* last MsgReceive id (server side) */
    char reply[QRTP_MSG_LEN];
    size_t reply_len;
    int reply_ready;
} qnx_priv_t;

static int qnx_create(qrtp_transport_t *t) {
    qnx_priv_t *p = t->priv;
    p->chid = ChannelCreate(0);
    if (p->chid == -1)
        return -1;
    p->coid = ConnectAttach(ND_LOCAL_NODE, 0, p->chid, _NTO_SIDE_CHANNEL, 0);
    if (p->coid == -1) {
        ChannelDestroy(p->chid);
        p->chid = -1;
        return -1;
    }
    p->rcvid = 0;
    p->reply_len = 0;
    p->reply_ready = 0;
    return 0;
}

static int qnx_destroy(qrtp_transport_t *t) {
    qnx_priv_t *p = t->priv;
    int rc = 0;
    if (p->coid != -1 && ConnectDetach(p->coid) == -1) rc = -1;
    if (p->chid != -1 && ChannelDestroy(p->chid) == -1) rc = -1;
    p->coid = p->chid = -1;
    return rc;
}

static int qnx_send(qrtp_transport_t *t, int ep, const void *buf, size_t len) {
    qnx_priv_t *p = t->priv;
    if (ep == QRTP_EP_SERVER) {
        /* Server replies to the last received message. */
        if (p->rcvid == 0) return -1;
        if (MsgReply(p->rcvid, EOK, buf, (int)len) == -1) return -1;
        p->rcvid = 0;
        return 0;
    }
    if (ep == QRTP_EP_CLIENT) {
        /* MsgSend blocks until the server replies; keep the reply. */
        if (MsgSend(p->coid, buf, (int)len, p->reply, (int)sizeof(p->reply)) == -1)
            return -1;
        p->reply_len = sizeof(p->reply);
        p->reply_ready = 1;
        return 0;
    }
    return -1;
}

static int qnx_recv(qrtp_transport_t *t, int ep, void *buf, size_t len) {
    qnx_priv_t *p = t->priv;
    if (ep == QRTP_EP_SERVER) {
        struct _msg_info info;
        int rcvid = MsgReceive(p->chid, buf, (int)len, &info);
        if (rcvid == -1) return -1;
        p->rcvid = rcvid;
        return 0;
    }
    if (ep == QRTP_EP_CLIENT) {
        /* Reply already arrived with MsgSend; copy it out. */
        if (!p->reply_ready) return -1;
        size_t n = len < p->reply_len ? len : p->reply_len;
        memcpy(buf, p->reply, n);
        p->reply_ready = 0;
        return 0;
    }
    return -1;
}

static const qrtp_transport_ops_t qnx_ops = {
    .create = qnx_create,
    .destroy = qnx_destroy,
    .send = qnx_send,
    .recv = qnx_recv,
};

int qrtp_transport_qnx_create(qrtp_transport_t *t) {
    static qnx_priv_t priv = { .chid = -1, .coid = -1, .rcvid = 0 };
    if (!t) return -1;
    memset(&priv, 0, sizeof(priv));
    priv.chid = -1;
    priv.coid = -1;
    t->ops = &qnx_ops;
    t->priv = &priv;
    return t->ops->create(t);
}

#else /* !__QNX__ */
#error "transport_qnx.c compiles only under qcc (QNX SDP 8.0) — do not add it to the host build."
#endif
