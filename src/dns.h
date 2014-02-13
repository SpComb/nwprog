#ifndef DNS_H
#define DNS_H

#include "common/event.h"

#define DNS_SERVICE "53"

/* Limits */
#define DNS_LABEL 63
#define DNS_NAME 255

/* Per EDNS0... */
#define DNS_PACKET (4 * 1024)

enum dns_opcode {
    DNS_QUERY       = 0,
    DNS_IQUERY      = 1,
    DNS_STATUS      = 2,
};

enum dns_rcode {
    DNS_NOERROR     = 0,
    DNS_FMTERROR    = 1,
    DNS_SERVFAIL    = 2,
    DNS_NXDOMAIN    = 3,
    DNS_NOTIMPL     = 4,
    DNS_REFUSED     = 5,
};

enum dns_type {
    DNS_A           = 1,
    DNS_NS          = 2,
    DNS_CNAME       = 5,
    DNS_SOA         = 6,
    DNS_PTR         = 12,
    DNS_MX          = 15,
    DNS_TXT         = 16,

    DNS_QTYPE_AXFR  = 252,
    DNS_QTYPE_ANY   = 255,
};

enum dns_class {
    DNS_IN          = 1,
    DNS_CH          = 3,

    DNS_QCLASS_ANY  = 255,
};

/*
 * DNS resolver.
 */
struct dns;

/*
 * Create a new resolver client.
 */
int dns_create (struct event_main *event_main, struct dns **dnsp, const char *resolver);

/*
 * XXX: Perform a DNS lookup...
 */
int dns_resolve (struct dns *dns, const char *name, enum dns_type type);

/*
 * Release all associated resources.
 */
void dns_destroy (struct dns *dns);

#endif
