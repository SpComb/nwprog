#ifndef DNS_DNS_H
#define DNS_DNS_H

#include "../dns.h"

#include <stdint.h>
#include <stddef.h>

struct dns_packet {
    char buf[DNS_PACKET];

    char *ptr, *end;
};

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

struct dns_question {
    char            qname[DNS_NAME];
    uint16_t        qtype;
    uint16_t        qclass;
};

struct dns_record {
    char            name[DNS_NAME];
    uint16_t        type;
    uint16_t        class;
    uint32_t        ttl;
    uint16_t        rdlength;

    void            *rdatap;
};

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
