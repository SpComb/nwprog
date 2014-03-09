#include "dns.h"
#include "dns/dns.h"

#include "common/log.h"

#include <stdio.h>
#include <stdlib.h>

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

const char * dns_type_str (enum dns_type type)
{
    static char buf[32];

    switch (type) {
        case DNS_A:	            return "A";
        case DNS_NS:	        return "NS";
        case DNS_CNAME:	        return "CNAME";
        case DNS_SOA:	        return "SOA";
        case DNS_PTR:	        return "PTR";
        case DNS_MX:	        return "MX";
        case DNS_TXT:	        return "TXT";

        case DNS_AAAA:          return "AAAA";

        case DNS_QTYPE_AXFR:    return "AXFR";
        case DNS_QTYPE_ANY:	    return "ANY";

        default:
            snprintf(buf, sizeof(buf), "%d", type);
            return buf;
    }
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
