#include "dns.h"
#include "dns/dns.h"

#include "common/log.h"
#include "common/udp.h"
#include "common/util.h"

#include <stdlib.h>

struct dns {
    struct udp *udp;
};

const char * dns_type_str (enum dns_type type)
{
    switch (type) {
        case DNS_A:	            return "A";
        case DNS_NS:	        return "NS";
        case DNS_CNAME:	        return "CNAME";
        case DNS_SOA:	        return "SOA";
        case DNS_PTR:	        return "PTR";
        case DNS_MX:	        return "MX";
        case DNS_TXT:	        return "TXT";

        case DNS_QTYPE_AXFR:    return "AXFR";
        case DNS_QTYPE_ANY:	    return "ANY";

        default:                return "???";
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

    if ((err = udp_connect(event_main, &dns->udp, resolver, DNS_SERVICE))) {
        log_error("udp_connect %s:%s", resolver, DNS_SERVICE);
        goto error;
    }

    *dnsp = dns;
    
    return 0;

error:
    dns_destroy(dns);

    return -1;
}

int dns_query (struct dns *dns, const struct dns_header *header, const struct dns_question *question)
{
    struct dns_packet query = { };
    query.ptr = query.buf;

    // pack
    if (
            dns_pack_header(&query, header)
        ||  dns_pack_question(&query, question)
    ) {
        log_warning("query overflow");
        return 1;
    }

    // send
    size_t size = (query.ptr - query.buf);
    if (udp_write(dns->udp, query.buf, size)) {
        log_warning("udp_write: %zu", size);
        return -1;
    }

    // sent
    return 0;
}

int dns_resolve (struct dns *dns, const char *name, enum dns_type type)
{
    struct dns_header header = {
        .qr         = 1,
        .opcode     = DNS_QUERY,
        .rd         = 1,
        .qdcount    = 1,
    };
    struct dns_question question = {
        .qtype      = type,
        .qclass     = DNS_IN,
    };
    int err;
    
    log_info("%s %s?", name, dns_type_str(type));
    
    if (str_copy(question.qname, sizeof(question.qname), name)) {
        log_warning("qname overflow: %s", name);
        return 1;
    }

    if ((err = dns_query(dns, &header, &question))) {
        log_error("dns_query: %s %s?", name, dns_type_str(question.qtype));
        return err;
    }

    // TODO: response
    return 0;
}

void dns_destroy (struct dns *dns)
{
    if (dns->udp)
        udp_destroy(dns->udp);

    free(dns);
}

