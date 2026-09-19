/* KallistiOS ##version##

   libppp/lcp.c
   Copyright (C) 2007, 2014 Lawrence Sebald
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <arpa/inet.h>

#include <arch/timer.h>

#include <ppp/ppp.h>

#include "ppp_internal.h"

/* RFC 1661 states that many packets must have "unique identifiers" for each
   instance sent across the link. We maintain a list of what the last identifier
   used for each of these types is here. */
struct lcp_state_s {
    int state;
    uint8_t last_conf;
    uint8_t last_term;
    uint8_t last_coderej;
    uint8_t last_protrej;
    uint8_t last_echo;
    uint8_t last_discard;

    ppp_state_t *ppp_state;

    uint64_t next_resend;
    uint16_t resend_cnt;
    int (*resend_pkt)(ppp_protocol_t *, int);
    void (*resend_timeout)(ppp_protocol_t *);
} lcp_state;

/* Constants for the Configure packets (options to be configured) */
#define LCP_CONFIGURE_VENDOR          0    /* XXXX: not supported. */
#define LCP_CONFIGURE_MRU             1
#define LCP_CONFIGURE_ACCM            2
#define LCP_CONFIGURE_AUTH_PROTO      3
#define LCP_CONFIGURE_QUALITY_PROTO   4    /* XXXX: not supported. */
#define LCP_CONFIGURE_MAGIC_NUMBER    5
#define LCP_CONFIGURE_PROTO_COMP      7
#define LCP_CONFIGURE_ADDR_CTRL_COMP  8

static void lcp_cfg_timeout(ppp_protocol_t *self) {
    (void)self;

    /* The link is probably dead here... */
    lcp_state.resend_pkt = NULL;
    lcp_state.resend_timeout = NULL;
    _ppp_enter_phase(PPP_PHASE_DEAD);
}

static int lcp_send_client_cfg(ppp_protocol_t *self, int resend) {
    uint8_t rawpkt[100];
    lcp_pkt_t *pkt = (lcp_pkt_t *)rawpkt;
    int len = 0;
    uint32_t accm = lcp_state.ppp_state->in_accm[0];

    (void)self;

    /* Fill in the code and identifier, then move onto the data. We'll get the
       length when we're done with the data. */
    pkt->code = LCP_CONFIGURE_REQUEST;

    if(resend)
        pkt->id = lcp_state.last_conf;
    else
        pkt->id = ++lcp_state.last_conf;

    if(!(lcp_state.ppp_state->our_flags & PPP_FLAG_NO_ACCM)) {
        /* Asynchronous control character map. Clear the whole thing. */
        pkt->data[len++] = LCP_CONFIGURE_ACCM;
        pkt->data[len++] = 6;
        pkt->data[len++] = (uint8_t)(accm >> 24);
        pkt->data[len++] = (uint8_t)(accm >> 16);
        pkt->data[len++] = (uint8_t)(accm >> 8);
        pkt->data[len++] = (uint8_t)accm;
    }

    if(lcp_state.ppp_state->our_flags & PPP_FLAG_MAGIC_NUMBER) {
        uint32_t m = lcp_state.ppp_state->our_magic;

        pkt->data[len++] = LCP_CONFIGURE_MAGIC_NUMBER;
        pkt->data[len++] = 6;
        pkt->data[len++] = (uint8_t)(m >> 24);
        pkt->data[len++] = (uint8_t)(m >> 16);
        pkt->data[len++] = (uint8_t)(m >> 8);
        pkt->data[len++] = (uint8_t)m;
    }

    if(lcp_state.ppp_state->our_flags & PPP_FLAG_PCOMP) {
        pkt->data[len++] = LCP_CONFIGURE_PROTO_COMP;
        pkt->data[len++] = 2;
    }

    if(lcp_state.ppp_state->our_flags & PPP_FLAG_ACCOMP) {
        pkt->data[len++] = LCP_CONFIGURE_ADDR_CTRL_COMP;
        pkt->data[len++] = 2;
    }

    if(lcp_state.ppp_state->our_flags & PPP_FLAG_WANT_MRU) {
        pkt->data[len++] = LCP_CONFIGURE_MRU;
        pkt->data[len++] = 4;
        pkt->data[len++] = (uint8_t)(1500 >> 8);
        pkt->data[len++] = (uint8_t)1500;
    }

    /* All other options we care about are their default values, so we shouldn't
       have to worry about them. */

    len += 4;
    pkt->len = htons(len);

    /* Set the resend timer for 3 seconds. */
    lcp_state.next_resend = timer_ms_gettime64() + 3000;
    lcp_state.resend_pkt = &lcp_send_client_cfg;
    lcp_state.resend_timeout = &lcp_cfg_timeout;

    if(!resend)
        lcp_state.resend_cnt = 10;

    return ppp_send(rawpkt, len, PPP_PROTOCOL_LCP);
}

static int lcp_send_code_reject(ppp_protocol_t *self, const uint8_t *pkt,
                                size_t len) {
    uint8_t buf[len + 4];
    lcp_pkt_t *lcp = (lcp_pkt_t *)buf;
    uint16_t lcp_len = len + 4;

    (void)self;

    /* See if we need to truncate whatever is in the packet... */
    if(lcp_len > lcp_state.ppp_state->peer_mru)
        lcp_len = lcp_state.ppp_state->peer_mru;

    /* Fill in the packet itself. */
    lcp->code = LCP_CODE_REJECT;
    lcp->id = ++lcp_state.last_coderej;
    lcp->len = htons(lcp_len);

    /* Copy in the data. */
    memcpy(buf + 4, pkt, lcp_len - 4);

    /* Send it away. */
    return ppp_send(buf, lcp_len, PPP_PROTOCOL_LCP);
}

static int lcp_send_terminate_ack(ppp_protocol_t *self, uint8_t id,
                                  const uint8_t *data, size_t len) {
    uint8_t buf[len + 4];
    lcp_pkt_t *pkt = (lcp_pkt_t *)buf;

    (void)self;

    /* Fill in the packet header. */
    pkt->code = LCP_TERMINATE_ACK;
    pkt->id = id;
    pkt->len = htons(len + 4);

    /* Copy the data over. */
    if(data)
        memcpy(buf + 4, data, len);

    /* Send it away. */
    return ppp_send(buf, len + 4, PPP_PROTOCOL_LCP);
}

static int lcp_send_echo_reply(ppp_protocol_t *self, const lcp_pkt_t *pkt,
                               size_t len) {
    uint8_t buf[len];
    lcp_pkt_t *lcp = (lcp_pkt_t *)buf;
    uint32_t *magic = (uint32_t *)(buf + 4);

    (void)self;

    /* Copy over the echo request. */
    memcpy(buf, pkt, len);

    /* Update what we have to. */
    lcp->code = LCP_ECHO_REPLY;
    *magic = lcp_state.ppp_state->our_magic;

    /* Send it away. */
    return ppp_send(buf, len, PPP_PROTOCOL_LCP);
}

static int lcp_handle_configure_req(ppp_protocol_t *self, const lcp_pkt_t *pkt,
                                    size_t len) {
    size_t ptr = 0;
    uint16_t tmp16;
    uint8_t opt_len;
    uint8_t response[1500], nak[1500];
    uint8_t response_code = LCP_CONFIGURE_ACK;
    uint8_t response_len = 4, nak_len = 4;

    /* Parameters and their default values. */
    uint16_t auth_proto = 0, mru = 1500;
    uint32_t accm = 0xffffffff, magic = 0, flags = 0;

    (void)pkt;

    switch(lcp_state.state) {
        case PPP_STATE_CLOSING:
        case PPP_STATE_STOPPING:
            /* Silently discard and don't move states. */
            return 0;

        case PPP_STATE_CLOSED:
            /* Send a terminate ack and discard the request. */
            return lcp_send_terminate_ack(self, pkt->id, NULL, 0);

        case PPP_STATE_OPENED:
            /* XXXX: This layer down. */
            /* Fall through... */
            __fallthrough;

        case PPP_STATE_STOPPED:
            lcp_send_client_cfg(self, 0);
            lcp_state.state = PPP_STATE_REQUEST_SENT;
            break;
    }

    DBG("lcp: Peer configure request received with opts:\n");

    /* Check each configuration option to see what we will reply with. */
    len -= 4;

    while(ptr < len) {
        if(len - ptr < 2) {
            /* Hopefully the peer will resend us a configure request with a sane
               set of options. For now, just ignore the bad request. */
            DBG("lcp: bad configure length, ignoring.\n");
            return -1;
        }

        /* Parse out the length of this option. */
        opt_len = pkt->data[ptr + 1];

        if(opt_len < 2 || ptr + opt_len > len) {
            /* Another sign we have a bad packet. Just ignore it and wait for
               the peer to resend it. */
            DBG("lcp: bad option length, ignoring packet\n");
            return -1;
        }

        /* Go through each parameter checking if it is sane. Check if each
           option is known to the implementation, whether it is of an
           appropriate length, and if the value is acceptable.
           Note that RFC 1661 says that options with invalid lengths should be
           NAKed and that an appropriate value should be sent to the peer. It
           seems, however, that other PPP implementations do not do this and
           instead reject options with invalid lengths outright. We follow the
           lead of these other implementations for sanity's sake, especially
           with the basic options that are defined in the RFC. */
        switch(pkt->data[ptr]) {
            case LCP_CONFIGURE_MRU:
                if(opt_len == 4) {
                    mru = (pkt->data[ptr + 2] << 8) | pkt->data[ptr + 3];
                    DBG("    peer mru: %hu\n", mru);

                    /* If the peer is requesting an MRU greater than 1500, just
                       use 1500. It won't hurt to send them smaller packets
                       anyway. */
                    if(mru > 1500)
                        mru = 1500;
                }
                else {
                    DBG("    peer mru (bad length)\n");
                    goto reject_opt;
                }
                break;

            case LCP_CONFIGURE_ACCM:
                if(opt_len == 6) {
                    accm = (pkt->data[ptr + 2] << 24) |
                        (pkt->data[ptr + 3] << 16) | (pkt->data[ptr + 4] << 8) |
                        pkt->data[ptr + 5];
                    DBG("    peer accm: %08x\n", (unsigned int)accm);
                }
                else {
                    DBG("    peer accm (bad length)\n");
                    goto reject_opt;
                }
                break;

            case LCP_CONFIGURE_AUTH_PROTO:
                if(opt_len < 4) {
                    DBG("    auth protocol (bad length)\n");
                    goto reject_opt;
                }

                /* Grab the protocol requested from the option. */
                tmp16 = (pkt->data[ptr + 2] << 8) | pkt->data[ptr + 3];
                DBG("    auth protocol: %04x ", tmp16);

                /* Check if we'll accept this protocol. */
                if(tmp16 == PPP_PROTOCOL_PAP) {
                    /* If we've already accepted something else, or the length
                       is not as we expect it, reject this option. */
                    if(opt_len != 4) {
                        DBG("(PAP -- bad length)\n");
                        goto reject_opt;
                    }
                    else if(auth_proto && auth_proto != tmp16) {
                        DBG("(PAP -- rejecting duplicate)\n");
                        goto reject_opt;
                    }
                    else {
                        auth_proto = tmp16;
                        DBG("(PAP)\n");
                    }
                }
                else {
                    DBG("(Unknown -- NAKing)\n");

                    if(response_code != LCP_CONFIGURE_REJECT)  {
                        if(nak_len + 4 < 1500) {
                            response_code = LCP_CONFIGURE_NAK;
                            nak[nak_len++] = LCP_CONFIGURE_AUTH_PROTO;
                            nak[nak_len++] = 4;
                            nak[nak_len++] = (uint8_t)(PPP_PROTOCOL_PAP >> 8);
                            nak[nak_len++] = (uint8_t)(PPP_PROTOCOL_PAP);
                        }
                    }
                }

                break;

            case LCP_CONFIGURE_MAGIC_NUMBER:
                if(opt_len == 6) {
                    magic = (pkt->data[ptr + 2] << 24) |
                        (pkt->data[ptr + 3] << 16) | (pkt->data[ptr + 4] << 8) |
                        pkt->data[ptr + 5];
                    flags |= PPP_FLAG_MAGIC_NUMBER;
                    DBG("    peer magic: %08x\n", (unsigned int)magic);
                }
                else {
                    DBG("    peer magic (bad length)\n");
                    goto reject_opt;
                }
                break;

            case LCP_CONFIGURE_PROTO_COMP:
                if(opt_len == 2) {
                    flags |= PPP_FLAG_PCOMP;
                    DBG("    protocol compression on\n");
                }
                else {
                    DBG("    protocol compression (bad length)\n");
                    goto reject_opt;
                }
                break;

            case LCP_CONFIGURE_ADDR_CTRL_COMP:
                if(opt_len == 2) {
                    flags |= PPP_FLAG_ACCOMP;
                    DBG("    addr/ctrl compression on\n");
                }
                else {
                    DBG("    addr/ctrl compression (bad length)\n");
                    goto reject_opt;
                }
                break;

            /* If we don't know about the option, reject it. */
            default:
                DBG("    unknown option: %d (len %d)\n", pkt->data[ptr],
                    opt_len);
reject_opt:
                if(response_len + opt_len < 1500) {
                    response_code = LCP_CONFIGURE_REJECT;
                    memcpy(response + response_len, &pkt->data[ptr], opt_len);
                    response_len += opt_len;
                }
        }

        ptr += opt_len;
    }

    /* Respond to the packet appropriately. */
    if(response_code == LCP_CONFIGURE_ACK) {
        int rv = 0;
        ppp_state_t *st = lcp_state.ppp_state;

        memcpy(response, pkt, len + 4);
        response[0] = LCP_CONFIGURE_ACK;
        rv = ppp_send(response, len + 4, PPP_PROTOCOL_LCP);

        /* Save all the accepted configuration options in the ppp state. */
        st->peer_flags = flags;
        st->peer_magic = magic;
        st->out_accm[0] = accm;
        st->auth_proto = auth_proto;
        st->peer_mru = mru;

        if(lcp_state.state == PPP_STATE_ACK_RECEIVED) {
            lcp_state.state = PPP_STATE_OPENED;

            /* The resend timer isn't running in the opened state, so clear it
               now. */
            lcp_state.resend_pkt = NULL;
            lcp_state.resend_timeout = NULL;

            /* XXXX: This layer up. */

            /* Did the peer ask us to auth? If not, go to the network phase. If
               so, go to the authentication phase. */
            if(auth_proto == 0)
                _ppp_enter_phase(PPP_PHASE_NETWORK);
            else
                _ppp_enter_phase(PPP_PHASE_AUTHENTICATE);
        }
        else {
            lcp_state.state = PPP_STATE_ACK_SENT;
        }

        return rv;
    }
    else if(response_code == LCP_CONFIGURE_REJECT) {
        lcp_pkt_t *out = (lcp_pkt_t *)response;

        out->code = LCP_CONFIGURE_REJECT;
        out->id = pkt->id;
        out->len = htons(response_len);

        if(lcp_state.state != PPP_STATE_ACK_RECEIVED)
            lcp_state.state = PPP_STATE_REQUEST_SENT;

        return ppp_send(response, response_len, PPP_PROTOCOL_LCP);
    }
    else {
        lcp_pkt_t *out = (lcp_pkt_t *)nak;

        out->code = LCP_CONFIGURE_NAK;
        out->id = pkt->id;
        out->len = htons(nak_len);

        if(lcp_state.state != PPP_STATE_ACK_RECEIVED)
            lcp_state.state = PPP_STATE_REQUEST_SENT;

        return ppp_send(nak, nak_len, PPP_PROTOCOL_LCP);
    }

    return 0;
}

static int lcp_handle_configure_ack(ppp_protocol_t *self, const lcp_pkt_t *pkt,
                                    size_t len) {
    (void)self;
    (void)len;

    if(pkt->id != lcp_state.last_conf) {
        DBG("lcp: received configure ack with an invalid identifier\n");
        return -1;
    }

    DBG("lcp: received configure ack\n");

    /* XXXX: It would be a good idea to make sure the peer ACKed everything we
       sent properly... */

    switch(lcp_state.state) {
        case PPP_STATE_CLOSING:
        case PPP_STATE_STOPPING:
            /* Silently discard. */
            return 0;

        case PPP_STATE_CLOSED:
        case PPP_STATE_STOPPED:
            return lcp_send_terminate_ack(self, pkt->id, NULL, 0);

        case PPP_STATE_REQUEST_SENT:
            /* The state diagram says to initialize the resend counter here, but
               that doesn't really make much sense to me... Oh well, I guess the
               spec is probably right? */
            lcp_state.resend_cnt = 10;
            lcp_state.state = PPP_STATE_ACK_RECEIVED;
            return 0;

        case PPP_STATE_OPENED:
            /* Uh oh... Something's gone wrong. */
            /* XXXX: This layer down. */
            /* Fall through... */
            __fallthrough;

        case PPP_STATE_ACK_RECEIVED:
            /* Well, this isn't good... Resend the configure request and back
               down to the request sent state we go... */
            lcp_state.state = PPP_STATE_REQUEST_SENT;
            return lcp_send_client_cfg(self, 0);

        case PPP_STATE_ACK_SENT:
            lcp_state.resend_pkt = NULL;
            lcp_state.resend_timeout = NULL;
            lcp_state.state = PPP_STATE_OPENED;
            /* XXXX: This layer up. */

            /* Did the peer ask us to auth? If not, go to the network phase. If
               so, go to the authentication phase. */
            if(lcp_state.ppp_state->auth_proto == 0)
                _ppp_enter_phase(PPP_PHASE_NETWORK);
            else
                _ppp_enter_phase(PPP_PHASE_AUTHENTICATE);

            return 0;
    }

    return 0;
}

static int lcp_handle_configure_nak(ppp_protocol_t *self, const lcp_pkt_t *pkt,
                                    size_t len) {
    uint32_t flags = lcp_state.ppp_state->our_flags;
    uint32_t accm = lcp_state.ppp_state->in_accm[0];
    uint32_t magic = lcp_state.ppp_state->our_magic;
    uint16_t mru = 1500;
    size_t ptr = 0;
    uint8_t opt_len;

    (void)self;

    if(pkt->id != lcp_state.last_conf) {
        DBG("lcp: received configure nak with an invalid identifier\n");
        return -1;
    }

    switch(lcp_state.state) {
        case PPP_STATE_CLOSING:
        case PPP_STATE_STOPPING:
            /* Silently discard and don't move states. */
            return 0;

        case PPP_STATE_CLOSED:
        case PPP_STATE_STOPPED:
            /* Send a terminate ack and discard the request. */
            return lcp_send_terminate_ack(self, pkt->id, NULL, 0);

        case PPP_STATE_OPENED:
            /* XXXX: This layer down. */
            /* Fall through... */
            __fallthrough;

        case PPP_STATE_REQUEST_SENT:
        case PPP_STATE_ACK_RECEIVED:
            lcp_state.state = PPP_STATE_REQUEST_SENT;
            break;
    }

    DBG("lcp: peer sent configure nak with opts:\n");

    /* Check each configuration option to see what we will reply with. */
    len -= 4;

    while(ptr < len) {
        if(len - ptr < 2) {
            /* Hopefully the peer will resend us a configure nak with a sane
               set of options. For now, just ignore the bad request. */
            DBG("lcp: bad configure length, ignoring.\n");
            return -1;
        }

        /* Parse out the length of this option. */
        opt_len = pkt->data[ptr + 1];

        if(opt_len < 2 || ptr + opt_len > len) {
            /* Another sign we have a bad packet. Just ignore it and wait for
               the peer to resend it. */
            DBG("lcp: bad option length, ignoring packet\n");
            return -1;
        }

        /* Go through each parameter checking if it is sane. Check if each
           option is known to the implementation, whether it is of an
           appropriate length, and if the value is acceptable.
           We ignore any options with lengths we don't understand. */
        switch(pkt->data[ptr]) {
            case LCP_CONFIGURE_MRU:
                if(opt_len == 4) {
                    mru = (pkt->data[ptr + 2] << 8) | pkt->data[ptr + 3];
                    DBG("    mru: %hu\n", mru);

                    /* Ignore whatever the peer suggested. We're going to use
                       1500 either way. If they suggested something smaller, it
                       won't hurt us to use 1500. If they suggested something
                       bigger, well... we don't support it. */
                    mru = 1500;
                    flags |= PPP_FLAG_WANT_MRU;
                }
                else {
                    DBG("    mru (bad length)\n");
                }
                break;

            case LCP_CONFIGURE_ACCM:
                /* No idea why they'd NAK our ACCM... but I guess we should just
                   accept their idea of it? */
                if(opt_len == 6) {
                    accm = (pkt->data[ptr + 2] << 24) |
                        (pkt->data[ptr + 3] << 16) | (pkt->data[ptr + 4] << 8) |
                        pkt->data[ptr + 5];
                    DBG("    accm: %08x\n", (unsigned int)accm);
                }
                else {
                    DBG("    accm (bad length)\n");
                }
                break;

            case LCP_CONFIGURE_AUTH_PROTO:
                /* We don't ever require authentication... */
                DBG("    auth protocol (ignored)\n");
                break;

            case LCP_CONFIGURE_MAGIC_NUMBER:
                if(opt_len == 6) {
                    magic = (pkt->data[ptr + 2] << 24) |
                        (pkt->data[ptr + 3] << 16) | (pkt->data[ptr + 4] << 8) |
                        pkt->data[ptr + 5];
                    flags |= PPP_FLAG_MAGIC_NUMBER;
                    DBG("    magic: %08x\n", (unsigned int)magic);
                }
                else {
                    DBG("    magic (bad length)\n");
                }
                break;

            case LCP_CONFIGURE_PROTO_COMP:
                /* This shouldn't be in a configure nak, if it is treat it as if
                   the option was rejected in a configure reject. */
                if(opt_len == 2) {
                    flags &= ~PPP_FLAG_PCOMP;
                    DBG("    protocol compression\n");
                }
                else {
                    DBG("    protocol compression (bad length)\n");
                }
                break;

            case LCP_CONFIGURE_ADDR_CTRL_COMP:
                /* This shouldn't be in a configure nak, if it is treat it as if
                   the option was rejected in a configure reject. */
                if(opt_len == 2) {
                    flags &= ~PPP_FLAG_ACCOMP;
                    DBG("    addr/ctrl compression\n");
                }
                else {
                    DBG("    addr/ctrl compression (bad length)\n");
                }
                break;

            /* If we don't know about the option, ignore it. */
            default:
                DBG("    unknown option: %d (len %d)\n", pkt->data[ptr],
                    opt_len);
        }

        ptr += opt_len;
    }

    /* Save the options that the other side requested and resend the configure
       request. */
    lcp_state.ppp_state->our_flags = flags;
    lcp_state.ppp_state->our_magic = magic;
    lcp_state.ppp_state->in_accm[0] = accm;

    return lcp_send_client_cfg(self, 0);
}

static int lcp_handle_configure_rej(ppp_protocol_t *self, const lcp_pkt_t *pkt,
                                    size_t len) {
    uint32_t flags = lcp_state.ppp_state->our_flags;
    uint32_t accm = lcp_state.ppp_state->in_accm[0];
    uint32_t magic = lcp_state.ppp_state->our_magic;
    uint16_t mru = 1500;
    size_t ptr = 0;
    uint8_t opt_len;

    (void)self;

    if(pkt->id != lcp_state.last_conf) {
        DBG("lcp: received configure reject with an invalid identifier\n");
        return -1;
    }

    switch(lcp_state.state) {
        case PPP_STATE_CLOSING:
        case PPP_STATE_STOPPING:
            /* Silently discard and don't move states. */
            return 0;

        case PPP_STATE_CLOSED:
        case PPP_STATE_STOPPED:
            /* Send a terminate ack and discard the request. */
            return lcp_send_terminate_ack(self, pkt->id, NULL, 0);

        case PPP_STATE_OPENED:
            /* XXXX: This layer down. */
            /* Fall through... */
            __fallthrough;

        case PPP_STATE_REQUEST_SENT:
        case PPP_STATE_ACK_RECEIVED:
            lcp_state.state = PPP_STATE_REQUEST_SENT;
            break;
    }

    DBG("lcp: peer sent configure reject with opts:\n");

    /* Check each configuration option to see what we will reply with. */
    len -= 4;

    while(ptr < len) {
        if(len - ptr < 2) {
            /* Hopefully the peer will resend us a configure nak with a sane
               set of options. For now, just ignore the bad request. */
            DBG("lcp: bad configure length, ignoring.\n");
            return -1;
        }

        /* Parse out the length of this option. */
        opt_len = pkt->data[ptr + 1];

        if(opt_len < 2 || ptr + opt_len > len) {
            /* Another sign we have a bad packet. Just ignore it and wait for
               the peer to resend it. */
            DBG("lcp: bad option length, ignoring packet\n");
            return -1;
        }

        /* Go through each parameter checking if it is sane. Check if each
           option is known to the implementation, whether it is of an
           appropriate length, and if the value is acceptable.
           We ignore any options with lengths we don't understand. */
        switch(pkt->data[ptr]) {
            case LCP_CONFIGURE_MRU:
                if(opt_len == 4) {
                    mru = (pkt->data[ptr + 2] << 8) | pkt->data[ptr + 3];
                    DBG("    mru: %hu\n", mru);

                    /* Ignore whatever the peer suggested. We're going to use
                       1500 either way. If they suggested something smaller, it
                       won't hurt us to use 1500. If they suggested something
                       bigger, well... we don't support it. */
                    mru = 1500;
                    flags &= ~PPP_FLAG_WANT_MRU;
                }
                else {
                    DBG("    mru (bad length)\n");
                }
                break;

            case LCP_CONFIGURE_ACCM:
                /* No idea why they'd reject our ACCM... I guess we just don't
                   send it back again?... */
                if(opt_len == 6) {
                    accm = (pkt->data[ptr + 2] << 24) |
                        (pkt->data[ptr + 3] << 16) | (pkt->data[ptr + 4] << 8) |
                        pkt->data[ptr + 5];
                    DBG("    accm: %08x\n", (unsigned int)accm);
                    flags |= PPP_FLAG_NO_ACCM;
                }
                else {
                    DBG("    accm (bad length)\n");
                }
                break;

            case LCP_CONFIGURE_AUTH_PROTO:
                /* We don't ever require authentication... */
                DBG("    auth protocol (ignored)\n");
                break;

            case LCP_CONFIGURE_MAGIC_NUMBER:
                if(opt_len == 6) {
                    magic = (pkt->data[ptr + 2] << 24) |
                        (pkt->data[ptr + 3] << 16) | (pkt->data[ptr + 4] << 8) |
                        pkt->data[ptr + 5];
                    flags &= ~PPP_FLAG_MAGIC_NUMBER;
                    DBG("    magic: %08x\n", (unsigned int)magic);
                }
                else {
                    DBG("    magic (bad length)\n");
                }
                break;

            case LCP_CONFIGURE_PROTO_COMP:
                if(opt_len == 2) {
                    flags &= ~PPP_FLAG_PCOMP;
                    DBG("    protocol compression\n");
                }
                else {
                    DBG("    protocol compression (bad length)\n");
                }
                break;

            case LCP_CONFIGURE_ADDR_CTRL_COMP:
                if(opt_len == 2) {
                    flags &= ~PPP_FLAG_ACCOMP;
                    DBG("    addr/ctrl compression\n");
                }
                else {
                    DBG("    addr/ctrl compression (bad length)\n");
                }
                break;

            /* If we don't know about the option, ignore it. We obviously didn't
               actually send it in the first place. */
            default:
                DBG("    unknown option: %d (len %d)\n", pkt->data[ptr],
                    opt_len);
        }

        ptr += opt_len;
    }

    /* Save the options that the other side requested and resend the configure
       request. */
    lcp_state.ppp_state->our_flags = flags;

    return lcp_send_client_cfg(self, 0);
}

static int lcp_handle_terminate_req(ppp_protocol_t *self, const lcp_pkt_t *pkt,
                                    size_t len) {
    (void)len;

    switch(lcp_state.state) {
        case PPP_STATE_STOPPED:
        case PPP_STATE_CLOSED:
        case PPP_STATE_CLOSING:
        case PPP_STATE_STOPPING:
        case PPP_STATE_REQUEST_SENT:
            /* No state transitions needed. */
            break;

        case PPP_STATE_ACK_RECEIVED:
        case PPP_STATE_ACK_SENT:
            lcp_state.state = PPP_STATE_REQUEST_SENT;
            break;

        case PPP_STATE_OPENED:
            /* XXXX: This layer down. */
            lcp_state.resend_pkt = NULL;
            lcp_state.resend_timeout = NULL;
            lcp_state.state = PPP_STATE_STOPPING;
            __fallthrough;

        default:
            return -1;
    }

    return lcp_send_terminate_ack(self, pkt->id, NULL, 0);
}

static int lcp_handle_terminate_ack(ppp_protocol_t *self, const lcp_pkt_t *pkt,
                                    size_t len) {
    (void)len;

    if(pkt->id != lcp_state.last_term) {
        DBG("lcp: received terminate ack with an invalid identifier\n");
        return -1;
    }

    switch(lcp_state.state) {
        case PPP_STATE_STOPPED:
        case PPP_STATE_CLOSED:
        case PPP_STATE_REQUEST_SENT:
        case PPP_STATE_ACK_SENT:
            /* Nothing to do. */
            break;

        case PPP_STATE_CLOSING:
            /* XXXX: This layer finished. */
            lcp_state.resend_pkt = NULL;
            lcp_state.resend_timeout = NULL;
            lcp_state.state = PPP_STATE_CLOSED;
            break;

        case PPP_STATE_STOPPING:
            /* XXXX: This layer finished. */
            lcp_state.resend_pkt = NULL;
            lcp_state.resend_timeout = NULL;
            lcp_state.state = PPP_STATE_STOPPED;
            break;

        case PPP_STATE_ACK_RECEIVED:
            lcp_state.state = PPP_STATE_REQUEST_SENT;
            break;

        case PPP_STATE_OPENED:
            /* XXXX: This layer down. */
            /* Something has gone wrong... Attempt to reconfigure the link. */
            lcp_state.state = PPP_STATE_REQUEST_SENT;
            return lcp_send_client_cfg(self, 0);

        default:
            return -1;
    }

    return 0;
}

/* PPP Protocol implementation functions. */
static int lcp_shutdown(ppp_protocol_t *self) {
    return ppp_del_protocol(self);
}

static int lcp_input(ppp_protocol_t *self, const uint8_t *buf, size_t len) {
    const lcp_pkt_t *pkt = (const lcp_pkt_t *)buf;

    (void)self;

    /* Sanity check. */
    if(len < sizeof(lcp_pkt_t) || len != ntohs(pkt->len)) {
        /* Drop bad packet. Should probably log this at some point. */
        return -1;
    }

    /* What type of packet did we get? */
    switch(pkt->code) {
        case LCP_CONFIGURE_REQUEST:
            return lcp_handle_configure_req(self, pkt, len);

        case LCP_CONFIGURE_ACK:
            return lcp_handle_configure_ack(self, pkt, len);

        case LCP_CONFIGURE_NAK:
            return lcp_handle_configure_nak(self, pkt, len);

        case LCP_CONFIGURE_REJECT:
            return lcp_handle_configure_rej(self, pkt, len);

        case LCP_TERMINATE_REQUEST:
            return lcp_handle_terminate_req(self, pkt, len);

        case LCP_TERMINATE_ACK:
            return lcp_handle_terminate_ack(self, pkt, len);

        case LCP_CODE_REJECT:
            /* This is only important if we're in the ack received state... We
               probably should log it though at some point... */
            if(lcp_state.state == PPP_STATE_ACK_RECEIVED)
                lcp_state.state = PPP_STATE_REQUEST_SENT;
            break;

        case LCP_PROTOCOL_REJECT:
            /* XXXX: Need to inform the protocol that got rejected. */
            break;

        case LCP_ECHO_REQUEST:
            return lcp_send_echo_reply(self, pkt, len);

        case LCP_ECHO_REPLY:
            /* We don't send echo requests yet, so no need to handle these. If
               we get an echo reply, just drop it silently for now. */
            break;

        case LCP_DISCARD_REQUEST:
            /* Silently discard, per the spec. */
            return 0;

        default:
            /* We don't know what this is, so send a code reject. */
            return lcp_send_code_reject(self, buf, len);
    }

    return 0;
}

static void lcp_enter_phase(ppp_protocol_t *self, int oldp, int newp) {
    (void)self;
    (void)oldp;

    /* We only care about when we're entering the establish phase. */
    if(newp == PPP_PHASE_ESTABLISH) {
        lcp_send_client_cfg(self, 0);
        lcp_state.state = PPP_STATE_REQUEST_SENT;
    }
}

static void lcp_check_timeouts(ppp_protocol_t *self, uint64_t tm) {
    if(lcp_state.resend_pkt && tm >= lcp_state.next_resend) {
        if(!lcp_state.resend_cnt) {
            lcp_state.resend_timeout(self);
        }
        else {
            lcp_state.resend_pkt(self, 1);
            --lcp_state.resend_cnt;
        }
    }
}

static ppp_protocol_t lcp_proto = {
    PPP_PROTO_ENTRY_INIT,
    "lcp",
    PPP_PROTOCOL_LCP,
    NULL,                   /* privdata */
    NULL,                   /* init */
    &lcp_shutdown,
    &lcp_input,
    &lcp_enter_phase,
    &lcp_check_timeouts
};

int ppp_lcp_send_proto_reject(uint16_t proto, const uint8_t *pkt, size_t len) {
    uint8_t buf[len + 6];
    lcp_pkt_t *lcp = (lcp_pkt_t *)buf;
    uint16_t lcp_len = len + 6;

    DBG("lcp: sending protocol reject for proto %04x\n", proto);

    /* Protocol rejects shall only be sent in the opened state. */
    if(lcp_state.state != PPP_STATE_OPENED)
        return 0;

    /* See if we need to truncate whatever is in the packet... */
    if(lcp_len > lcp_state.ppp_state->peer_mru)
        lcp_len = lcp_state.ppp_state->peer_mru;

    /* Fill in the packet itself. */
    lcp->code = LCP_PROTOCOL_REJECT;
    lcp->id = ++lcp_state.last_protrej;
    lcp->len = htons(lcp_len);

    /* Fill in the protocol field. */
    buf[4] = (uint8_t)(proto >> 8);
    buf[5] = (uint8_t)proto;

    /* Copy in the data. */
    memcpy(buf + 6, pkt, lcp_len - 6);

    /* Send it away. */
    int rv = ppp_send(buf, lcp_len, PPP_PROTOCOL_LCP);
    return rv;
}

int _ppp_lcp_init(ppp_state_t *st) {
    /* Clear all of the entries in the LCP identifier state. */
    memset(&lcp_state, 0, sizeof(lcp_state));

    lcp_state.ppp_state = st;

    return ppp_add_protocol(&lcp_proto);
}
