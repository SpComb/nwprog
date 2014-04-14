#ifndef DNS_H
#define DNS_H

#include "common/event.h"

#include <netinet/in.h>
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

/* Default for dns_create(.., resolver=NULL) */
#define DNS_RESOLVER "localhost"

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
    // RFC 1035
    DNS_A           = 1,
    DNS_NS          = 2,
    DNS_CNAME       = 5,
    DNS_SOA         = 6,
    DNS_PTR         = 12,
    DNS_MX          = 15,
    DNS_TXT         = 16,

    // RFC 3596
    DNS_AAAA        = 28,

    DNS_QTYPE_AXFR  = 252,
    DNS_QTYPE_ANY   = 255,

    DNS_TYPE_MAX,
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

/* enum -> static char * */
const char * dns_opcode_str (enum dns_opcode opcode);
const char * dns_rcode_str (enum dns_rcode rcode);
const char * dns_type_str (enum dns_type type);
const char * dns_class_str (enum dns_class class);
const char * dns_section_str (enum dns_section section);

/* char * -> dns_type */
int dns_type_parse (enum dns_type *typep, const char *str);

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
    struct in_addr  A;
    struct in6_addr AAAA;
    char        NS[DNS_NAME];
    char        CNAME[DNS_NAME];
    char        PTR[DNS_NAME];
    struct {
        uint16_t    preference;
        char        exchange[DNS_NAME];
    } MX;
};

/* rdata -> static char * representation of it (literal IPv4/IPv6 address etc) */
const char * dns_rdata_str (struct dns_record *rr, union dns_rdata *rdata);

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
 *
 * resolver:    external resolver host to query, or NULL for default.
 *
 * XXX: read /etc/resolv.conf... default is just "localhost" for now..
 */
int dns_create (struct event_main *event_main, struct dns **dnsp, const char *resolver);

/*
 * Perform a DNS lookup, without waiting for a response.
 */
int dns_resolve_async (struct dns *dns, struct dns_resolve **resolvep, const char *name, enum dns_type type);

/*
 * Perform a DNS lookup, returning the response in *resolvep.
 *
 * Implements 2s retries with a total 10s timeout.
 *
 * Returns <0 on internal error, DNS_NOERROR, dns_rcode >0 on resolve error.
 */
int dns_resolve (struct dns *dns, struct dns_resolve **resolvep, const char *name, enum dns_type type);

/*
 * Perform a DNS lookup with multiple queries for different types of the same name.
 * 
 * XXX: not really supported by anything.
 */
int dns_resolve_multi (struct dns *dns, struct dns_resolve **resolvep, const char *name, enum dns_type *types);

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
