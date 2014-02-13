#include "dns/dns.h"

#include "common/log.h"

#include <arpa/inet.h>
#include <string.h>

int dns_peek_u8 (struct dns_packet *pkt, uint8_t *u8p)
{
    uint8_t *in = (uint8_t *) pkt->ptr;

    if (pkt->ptr + sizeof(*in) > pkt->end)
        return 1;

    *u8p = *in;

    return 0;
}

int dns_unpack_u8 (struct dns_packet *pkt, uint8_t *u8p)
{
    uint8_t *in = (uint8_t *) pkt->ptr;

    pkt->ptr += sizeof(*in);

    if (pkt->ptr > pkt->end)
        return 1;

    *u8p = *in;

    log_debug("%u", *u8p);

    return 0;
}

int dns_unpack_u16 (struct dns_packet *pkt, uint16_t *u16p)
{
    uint16_t *in = (uint16_t *) pkt->ptr;

    pkt->ptr += sizeof(*in);

    if (pkt->ptr > pkt->end)
        return 1;

    *u16p = ntohs(*in);
    
    log_debug("%u", *u16p);

    return 0;
}

int dns_unpack_u32 (struct dns_packet *pkt, uint32_t *u32p)
{
    uint32_t *in = (uint32_t *) pkt->ptr;

    pkt->ptr += sizeof(*in);

    if (pkt->ptr > pkt->end)
        return 1;

    *u32p = ntohl(*in);
    
    log_debug("%u", *u32p);

    return 0;
}

int dns_unpack_buf (struct dns_packet *pkt, void *buf, size_t size)
{
    if (pkt->ptr + size > pkt->end)
        return 1;

    memcpy(buf, pkt->ptr, size);

    pkt->ptr += size;
    
    log_debug("#%zu", size);

    return 0;
}

int dns_unpack_ptr (struct dns_packet *pkt, void **ptrp, size_t size)
{
    if (pkt->ptr + size > pkt->end)
        return 1;

    *ptrp = pkt->ptr;

    pkt->ptr += size;
    
    log_debug("#%zu", size);

    return 0;
}

int dns_unpack_header (struct dns_packet *pkt, struct dns_header *header)
{
    uint16_t flags;

    return (
            dns_unpack_u16(pkt, &header->id)
        ||  dns_unpack_u16(pkt, &flags)
        ||  dns_unpack_u16(pkt, &header->qdcount)
        ||  dns_unpack_u16(pkt, &header->ancount)
        ||  dns_unpack_u16(pkt, &header->nscount)
        ||  dns_unpack_u16(pkt, &header->arcount)
    );

    header->qr      = flags >> 15 & 0x1;
    header->opcode  = flags >> 11 & 0xf;
    header->aa      = flags >> 10 & 0x1;
    header->tc      = flags >> 9  & 0x1;
    header->rd      = flags >> 8  & 0x1;
    header->rcode   = flags >> 0  & 0xf;

    return 0;
}

int dns_unpack_name (struct dns_packet *pkt, char *buf, size_t size)
{
    int err;
    uint8_t prefix;
    char *name = buf;

    while (!(err = dns_peek_u8(pkt, &prefix)) && prefix) {
        if (prefix & 0xc0) {
            uint16_t pointer;

            if (dns_unpack_u16(pkt, &pointer))
                return 1;

            // mask out prefix bits
            pointer &= ~0xc000;

            // TODO: implement support for label compression
            log_warning("unsupported compressed label @ %u", pointer);

        } else {
            uint8_t len;

            if (dns_unpack_u8(pkt, &len))
                return 1;

            if (name != buf) {
                // not the first item
                *name++ = '.';
            }

            if (name + len >= buf + size) {
                log_warning("name overflow: %zu + %u / %zu", (name - buf), len, size);
                return 1;
            }

            if (dns_unpack_buf(pkt, name, len))
                return 1;

            name[len] = '\0';

            log_debug("%u:%s", len, name);

            name += len;
        }
    }

    if (err)
        return err;

    uint8_t nul;

    if (dns_unpack_u8(pkt, &nul))
        return 1;

    *name = '\0';

    return 0;
}

int dns_unpack_question (struct dns_packet *pkt, struct dns_question *question)
{
    return (
            dns_unpack_name(pkt, question->qname, sizeof(question->qname))
        ||  dns_unpack_u16(pkt, &question->qtype)
        ||  dns_unpack_u16(pkt, &question->qclass)
    );
}

int dns_unpack_record (struct dns_packet *pkt, struct dns_record *rr)
{
    return (
            dns_unpack_name(pkt, rr->name, sizeof(rr->name))
        ||  dns_unpack_u16(pkt, &rr->type)
        ||  dns_unpack_u16(pkt, &rr->class)
        ||  dns_unpack_u32(pkt, &rr->ttl)
        ||  dns_unpack_u16(pkt, &rr->rdlength)
        ||  dns_unpack_ptr(pkt, &rr->rdatap, rr->rdlength)
    );
}

