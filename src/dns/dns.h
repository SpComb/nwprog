#ifndef DNS_DNS_H
#define DNS_DNS_H

#include "../dns.h"

#include <stddef.h>

struct dns_packet {
    char buf[DNS_PACKET];

    char *ptr, *end;
};

int dns_pack_header (struct dns_packet *pkt, const struct dns_header *header);
int dns_pack_name (struct dns_packet *pkt, const char *name);
int dns_pack_question (struct dns_packet *pkt, const struct dns_question *question);
int dns_pack_record (struct dns_packet *pkt, const struct dns_record *rr);

int dns_unpack_header (struct dns_packet *pkt, struct dns_header *header);
int dns_unpack_name (struct dns_packet *pkt, char *buf, size_t size);
int dns_unpack_question (struct dns_packet *pkt, struct dns_question *question);
int dns_unpack_record (struct dns_packet *pkt, struct dns_record *rr);
int dns_unpack_rdata (struct dns_packet *pkt, struct dns_record *rr, union dns_rdata *rdata);

#endif
