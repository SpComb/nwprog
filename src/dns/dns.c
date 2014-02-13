#include "dns.h"
#include "dns/dns.h"

#include "common/log.h"
#include "common/udp.h"
#include "common/util.h"

#include <stdlib.h>
#include <stdio.h>

struct dns {
    struct udp *udp;
};

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

        case DNS_QTYPE_AXFR:    return "AXFR";
        case DNS_QTYPE_ANY:	    return "ANY";

        default:
            snprintf(buf, sizeof(buf), "%d", type);
            return buf;
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
    query.end = query.buf + sizeof(query.buf);

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

int dns_response (struct dns *dns)
{
    struct dns_packet response = { };
    int err;

    // recv
    size_t size = sizeof(response.buf);

    if ((err = udp_read(dns->udp, response.buf, &size, NULL))) {
        log_warning("udp_read");
        return -1;
    }

    response.ptr = response.buf;
    response.end = response.buf + size;

    // unpack
    struct dns_header header;

    if ((err = dns_unpack_header(&response, &header))) {
        log_warning("dns_unpack_header");
        return err;
    }

    log_info("%s%s%s%s%s%s %s",
            header.qr       ? "QR " : "",
            dns_opcode_str(header.opcode),
            header.aa       ? " AA" : "",
            header.tc       ? " TC" : "",
            header.rd       ? " RD" : "",
            header.ra       ? " RA" : "",
            dns_rcode_str(header.rcode)
    );

    for (int i = 0; i < header.qdcount; i++) {
        struct dns_question question;

        if ((err = dns_unpack_question(&response, &question))) {
            log_warning("dns_unpack_question: %d", i);
            return err;
        }
        
        log_info("QD: %s %s:%s", question.qname, 
                dns_class_str(question.qclass),
                dns_type_str(question.qtype)
        );
    }
     
    for (int i = 0; i < header.ancount + header.nscount + header.arcount; i++) {
        struct dns_record rr;
        union dns_rdata rdata;

        if ((err = dns_unpack_record(&response, &rr))) {
            log_warning("dns_unpack_resource: %d", i);
            return err;
        }

        if ((err = dns_unpack_rdata(&response, &rr, &rdata))) {
            log_warning("dns_unpack_rdata: %d", i);
            return err;
        }

        const char *rrtag;

        if (i < header.ancount) {
            rrtag = "AN";

        } else if (i < header.ancount + header.nscount) {
            rrtag = "NS";

        } else if (i < header.ancount + header.nscount + header.arcount) {
            rrtag = "AR";

        } else {
            rrtag = "XX";
        }

        log_ninfo("%s: %s %s:%s %d ", rrtag, rr.name,
                dns_class_str(rr.class),
                dns_type_str(rr.type),
                rr.ttl
        );

        // decode
        uint8_t *u8;

        switch (rr.type) {
            case DNS_A:
                u8 = (uint8_t *) &rdata.A;

                log_qinfo("%d.%d.%d.%d", u8[3], u8[2], u8[1], u8[0]);

                break;

            case DNS_NS:
                log_qinfo("%s", rdata.NS);
                break;

            case DNS_CNAME:
                log_qinfo("%s", rdata.CNAME);
                break;

            case DNS_PTR:
                log_qinfo("%s", rdata.PTR);
                break;

            case DNS_MX:
                log_qinfo("%d:%s", rdata.MX.preference, rdata.MX.exchange);
                break;

            default:
                log_qinfo("%d:...", rr.rdlength);
                break;
        }
    }

    return 0;
}

int dns_resolve (struct dns *dns, const char *name, enum dns_type type)
{
    struct dns_header header = {
        .qr         = 0,
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

    if ((err = dns_response(dns))) {
        log_error("dns_response");
        return err;
    }

    return 0;
}

void dns_destroy (struct dns *dns)
{
    if (dns->udp)
        udp_destroy(dns->udp);

    free(dns);
}

