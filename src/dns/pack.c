#include "dns/dns.h"

#include "common/log.h"

#include <arpa/inet.h>
#include <string.h>

int dns_pack_u8 (struct dns_packet *pkt, uint8_t u8)
{
    uint8_t *out = (uint8_t *) pkt->ptr;

    pkt->ptr += sizeof(*out);

    if (pkt->ptr > pkt->end)
        return 1;

    *out = u8;

    return 0;
}

int dns_pack_u16 (struct dns_packet *pkt, uint16_t u16)
{
    uint16_t *out = (uint16_t *) pkt->ptr;

    pkt->ptr += sizeof(*out);

    if (pkt->ptr > pkt->end)
        return 1;

    *out = htons(u16);

    return 0;
}

int dns_pack_u32 (struct dns_packet *pkt, uint16_t u32)
{
    uint32_t *out = (uint32_t *) pkt->ptr;

    pkt->ptr += sizeof(*out);

    if (pkt->ptr > pkt->end)
        return 1;

    *out = htonl(u32);

    return 0;
}

int dns_pack_buf (struct dns_packet *pkt, const void *buf, size_t size)
{
    if (pkt->ptr + size > pkt->end)
        return 1;

    memcpy(pkt->ptr, buf, size);

    pkt->ptr += size;

    return 0;
}

int dns_pack_header (struct dns_packet *pkt, const struct dns_header *header)
{
    return (
            dns_pack_u16(pkt, header->id)
        ||  dns_pack_u16(pkt, (
                    header->qr      << 15
                |   header->opcode  << 11
                |   header->aa      << 10
                |   header->tc      << 9
                |   header->rd      << 8
                |   header->ra      << 7
                |   0               << 4
                |   header->rcode   << 0
            ))
        ||  dns_pack_u16(pkt, header->qdcount)
        ||  dns_pack_u16(pkt, header->ancount)
        ||  dns_pack_u16(pkt, header->nscount)
        ||  dns_pack_u16(pkt, header->arcount)
    );
}

int dns_pack_name (struct dns_packet *pkt, const char *name)
{
    char c, label[DNS_LABEL], *labelp = label;

    log_debug("%s", name);

    do {
        c = *name;

        if (*name)
            name++;

        // accumulate label
        if (c == '.' || c == '\0') {
            size_t len = (labelp - label);

            if (!len) {
                log_warning("skip empty label @ %zu:%s", strlen(name), name);

            } else {
                log_debug("%zu:%s @ %zu:%s", len, label, strlen(name), name);

                // 6-bit length
                if (
                        dns_pack_u8(pkt, len & 0x3f)
                    ||  dns_pack_buf(pkt, label, len)
                )
                    return 1;
            }

            labelp = label;
            *labelp = '\0';

        } else if (labelp + 1 < label + sizeof(label)) {
            *labelp++ = c;
            *labelp = '\0';
            continue;

        } else {
            log_error("label overflow: %s+%s", label, name);
            return 2;
        }

    } while (c);

    // end
    if (dns_pack_u8(pkt, 0x00))
        return 1;

    return 0;
}

int dns_pack_question (struct dns_packet *pkt, const struct dns_question *question)
{
    return (
            dns_pack_name(pkt, question->qname)
        ||  dns_pack_u16(pkt, question->qtype)
        ||  dns_pack_u16(pkt, question->qclass)
    );
}

int dns_pack_record (struct dns_packet *pkt, const struct dns_record *rr)
{
    return (
            dns_pack_name(pkt, rr->name)
        ||  dns_pack_u16(pkt, rr->type)
        ||  dns_pack_u16(pkt, rr->class)
        ||  dns_pack_u32(pkt, rr->ttl)
        ||  dns_pack_u16(pkt, rr->rdlength)
        ||  dns_pack_buf(pkt, rr->rdatap, rr->rdlength)
    );
}
