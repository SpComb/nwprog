#ifndef DNS_DNS_H
#define DNS_DNS_H

#include "../dns.h"

#include "common/udp.h"

#include <stddef.h>
#include <sys/queue.h>

/*
 * DNS resolver state for multiple dns_reolve's.
 */
struct dns {
    struct udp *udp;

    // query id pool
    uint16_t ids;

    TAILQ_HEAD(dns_resolves, dns_resolve) resolves;
};

struct dns_packet {
    char buf[DNS_PACKET];

    char *ptr, *end;
};

const char * dns_opcode_str (enum dns_opcode opcode);
const char * dns_rcode_str (enum dns_rcode rcode);
const char * dns_class_str (enum dns_class class);
const char * dns_type_str (enum dns_type type);
const char * dns_section_str (enum dns_section section);

int dns_pack_header (struct dns_packet *pkt, const struct dns_header *header);
int dns_pack_name (struct dns_packet *pkt, const char *name);
int dns_pack_question (struct dns_packet *pkt, const struct dns_question *question);
int dns_pack_record (struct dns_packet *pkt, const struct dns_record *rr);

int dns_unpack_header (struct dns_packet *pkt, struct dns_header *header);
int dns_unpack_name (struct dns_packet *pkt, char *buf, size_t size);
int dns_unpack_question (struct dns_packet *pkt, struct dns_question *question);
int dns_unpack_record (struct dns_packet *pkt, struct dns_record *rr);
int dns_unpack_rdata (struct dns_packet *pkt, struct dns_record *rr, union dns_rdata *rdata);

/*
 * The event used by this DNS resolver.
 */
static inline struct event * dns_event (struct dns *dns)
{
    return udp_event(dns->udp);
}

/*
 * Send a packed DNS query.
 *
 * If header is given, update the packed header before sending.
 */
int dns_query (struct dns *dns, struct dns_packet *packet, const struct dns_header *header);

/*
 * Recv a DNS response, unpacking the header.
 *
 * Returns 1 on timeout, <0 on error, 0 on success.
 */
int dns_response (struct dns *dns, struct dns_packet *packet, struct dns_header *header, const struct timeval *timeout);

#endif
