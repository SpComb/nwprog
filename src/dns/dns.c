#include "dns.h"
#include "dns/dns.h"

#include "common/log.h"
#include "common/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char * dns_opcode_str (enum dns_opcode opcode)
{
    switch (opcode) {
        case DNS_QUERY:         return "QUERY";
        case DNS_IQUERY:        return "IQUERY";
        case DNS_STATUS:        return "STATUS";
        default:                return "?????";
    }
}

const char * dns_rcode_str (enum dns_rcode rcode)
{
    switch (rcode) {
        case DNS_NOERROR:       return "NOERROR";
        case DNS_FMTERROR:      return "FMTERROR";
        case DNS_SERVFAIL:      return "SERVFAIL";
        case DNS_NXDOMAIN:      return "NXDOMAIN";
        case DNS_NOTIMPL:       return "NOTIMPL";
        case DNS_REFUSED:       return "REFUSED";
        default:                return "????????";
    }
}

const char * dns_class_str (enum dns_class class)
{
    switch (class) {
        case DNS_IN:            return "IN";
        case DNS_CH:            return "CH";

        default:                return "??";
    }
}

const char *dns_type_strs[DNS_TYPE_MAX] = {
    [DNS_A]         = "A",
    [DNS_NS]        = "NS",
    [DNS_CNAME]		= "CNAME",
    [DNS_SOA]		= "SOA",
    [DNS_PTR]		= "PTR",
    [DNS_MX]		= "MX",          
    [DNS_TXT]		= "TXT",         

    [DNS_AAAA]      = "AAAA",

    [DNS_QTYPE_AXFR]    = "AXFR",
    [DNS_QTYPE_ANY]     = "ANY",
};

const char * dns_type_str (enum dns_type type)
{
    if (type >= DNS_TYPE_MAX) {
        return NULL;
    } else if (dns_type_strs[type]) {
        return dns_type_strs[type];
    } else {
        static char buf[32];

        return str_fmt(buf, sizeof(buf), "%d", type);
    }
}

int dns_type_parse (enum dns_type *typep, const char *str)
{
    for (enum dns_type type = 0; type < DNS_TYPE_MAX; type++) {
        if (dns_type_strs[type] && strcasecmp(dns_type_strs[type], str) == 0) {
            *typep = type;
            return 0;
        }
    }

    return 1;
}

const char * dns_section_str (enum dns_section section)
{
    switch (section) {
        case DNS_QD:            return "QD";
        case DNS_AN:            return "AN";
        case DNS_AA:            return "AA";
        case DNS_AR:            return "AR";
        default:                return "??";
    }
}

int dns_create (struct event_main *event_main, struct dns **dnsp, const char *resolver)
{
    struct dns *dns;
    int err;

    if (!resolver) {
        // use default resolver
        resolver = DNS_RESOLVER;
    }

    if (!(dns = calloc(1, sizeof(*dns)))) {
        log_perror("calloc");
        return -1;
    }

    TAILQ_INIT(&dns->resolves);

    if ((err = udp_connect(event_main, &dns->udp, resolver, DNS_SERVICE))) {
        log_error("udp_connect %s:%s", resolver, DNS_SERVICE);
        goto error;
    }

    // start id pool
    dns->ids = random() % UINT16_MAX;

    *dnsp = dns;

    return 0;

error:
    dns_destroy(dns);

    return -1;
}

int dns_query (struct dns *dns, struct dns_packet *packet, const struct dns_header *header)
{
    packet->end = packet->ptr;
    packet->ptr = packet->buf;

    // re-pack header
    if (header) {
        if (dns_pack_header(packet, header)) {
            log_warning("query header overflow");
            return 1;
        }

        log_info("[%u] %s%s%s%s%s%s %s", header->id,
                header->qr       ? "QR " : "",
                dns_opcode_str(header->opcode),
                header->aa       ? " AA" : "",
                header->tc       ? " TC" : "",
                header->rd       ? " RD" : "",
                header->ra       ? " RA" : "",
                dns_rcode_str(header->rcode)
        );
    }

    // send
    size_t size = (packet->end - packet->buf);
    if (udp_write(dns->udp, packet->buf, size)) {
        log_warning("udp_write: %zu", size);
        return -1;
    }

    return 0;
}

int dns_response (struct dns *dns, struct dns_packet *packet, struct dns_header *header)
{
    int err;

    // recv
    size_t size = sizeof(packet->buf);

    if ((err = udp_read(dns->udp, packet->buf, &size, NULL))) {
        log_warning("udp_read");
        return -1;
    }

    packet->ptr = packet->buf;
    packet->end = packet->buf + size;

    // header
    if ((err = dns_unpack_header(packet, header))) {
        log_warning("dns_unpack_header");
        return err;
    }

    log_info("[%u] %s%s%s%s%s%s %s", header->id,
            header->qr      ? "QR " : "",
            dns_opcode_str(header->opcode),
            header->aa      ? " AA" : "",
            header->tc      ? " TC" : "",
            header->rd      ? " RD" : "",
            header->ra      ? " RA" : "",
            dns_rcode_str(header->rcode)
    );

    return 0;
}

void dns_destroy (struct dns *dns)
{
    if (dns->udp)
        udp_destroy(dns->udp);

    free(dns);
}
