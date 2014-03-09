#ifndef DNS_H
#define DNS_H

#include "common/event.h"

#include <stdint.h>

/* UDP service */
#define DNS_SERVICE "53"

/* Limits */
#define DNS_LABEL 63
#define DNS_NAME 255

/* Number of labels in a name */
#define DNS_LABELS 128

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

enum dns_section {
    DNS_QD,     // question
    DNS_AN,     // answer
    DNS_AA,     // authority
    DNS_AR,     // additional
};

/*
 * Fixed-size header.
 */
struct dns_header {
    uint16_t        id;

    uint16_t        qr      : 1;
    uint16_t        opcode  : 4;
    uint16_t        aa      : 1;
    uint16_t        tc      : 1;
    uint16_t        rd      : 1;
    uint16_t        ra      : 1;
    uint16_t        z       : 3;
    uint16_t        rcode   : 4;

    uint16_t        qdcount;
    uint16_t        ancount;
    uint16_t        nscount;
    uint16_t        arcount;
};

/*
 * Question record format.
 */
struct dns_question {
    char            qname[DNS_NAME];
    uint16_t        qtype;
    uint16_t        qclass;
};

/*
 * Response record format.
 */
struct dns_record {
    char            name[DNS_NAME];
    uint16_t        type;
    uint16_t        class;
    uint32_t        ttl;
    uint16_t        rdlength;

    void            *rdatap;
};

/*
 * Decoded response record data.
 */
union dns_rdata {
    uint32_t    A;
    char        NS[DNS_NAME];
    char        CNAME[DNS_NAME];
    char        PTR[DNS_NAME];
    struct {
        uint16_t    preference;
        char        exchange[DNS_NAME];
    } MX;
};

/*
 * DNS resolver.
 */
struct dns;

/*
 * Pending/processing DNS resolve query.
 */
struct dns_resolve;

/*
 * Create a new resolver client.
 */
int dns_create (struct event_main *event_main, struct dns **dnsp, const char *resolver);

/*
 * Perform a DNS lookup, returning the response in *resolvep.
 *
 * Returns <0 on internal error, DNS_NOERROR, dns_rcode >0 on resolve error.
 */
int dns_resolve (struct dns *dns, struct dns_resolve **resolvep, const char *name, enum dns_type type);

/*
 * Read out the reponse.
 */
int dns_resolve_header (struct dns_resolve *resolve, struct dns_header *header);

/*
 * Returns <0 on error, 0 on success, 1 on no more questions.
 */
int dns_resolve_question (struct dns_resolve *resolve, struct dns_question *question);

/*
 * The section of the record is returned in *section.
 *
 * Returns <0 on error, 0 on success, 1 on no more records.
 */
int dns_resolve_record (struct dns_resolve *resolve, enum dns_section *section, struct dns_record *rr, union dns_rdata *rdata);

/*
 * Release the resolver query.
 */
void dns_close (struct dns_resolve *resolve);

/*
 * Release all associated resources.
 */
void dns_destroy (struct dns *dns);

#endif
