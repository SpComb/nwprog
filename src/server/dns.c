#include "server/dns.h"

#include "common/log.h"
#include "../dns.h" // XXX: terrible naming failure

#include <string.h>

struct server_dns {
	/* Embed */
	struct server_handler handler;

    /* Shared across requests */
    struct dns *dns;
};

int server_dns_lookup (struct server_dns *s, struct server_client *client, const char *name, const char *type)
{
    int err;
    enum dns_type qtype;

    // parameters
    if (!name)
        return server_response_error(client, 400, NULL, "Missing required <tt>name=...</tt> parameter");

    if (!type)
        type = "A";

    if (dns_type_parse(&qtype, type))
        return server_response_error(client, 400, NULL, "Invalid <tt>type=...</tt> parameter");

    // resolve
    struct dns_resolve *resolve;

    if ((err = dns_resolve(s->dns, &resolve, name, qtype)) < 0) {
        log_error("dns_resolve: %s", name);
        return 500;
    }

    log_info("%s %s: %s", name, dns_type_str(qtype), dns_rcode_str(err));

    // response
    // err != DNS_NOERROR from dns_resolve?
    if (err) {
        log_warning("dns_resolve: %s: %d", name, err);
        server_response(client, 400, NULL);
    } else {
        server_response(client, 200, NULL);
    }

    server_response_header(client, "Content-Type", "text/plain");

    // output question/header
    struct dns_question qq;
    struct dns_header header;

    if ((err = dns_resolve_header(resolve, &header))) {
        log_warning("dns_resolve_header: %s", name);
        return -1;
    }

    server_response_print(client, ";; [%u] %s%s%s%s%s%s %s\n", header.id,
            header.qr      ? "QR " : "",
            dns_opcode_str(header.opcode),
            header.aa      ? " AA" : "",
            header.tc      ? " TC" : "",
            header.rd      ? " RD" : "",
            header.ra      ? " RA" : "",
            dns_rcode_str(header.rcode)
    );

    while (!(err = dns_resolve_question(resolve, &qq))) {
        server_response_print(client, "; %-30s %-5s %-10s?\n", qq.qname, dns_class_str(qq.qclass), dns_type_str(qq.qtype));
    }

    // output response
    enum dns_section section;
    struct dns_record rr;
    union dns_rdata rdata;
    enum dns_section current = -1;

    while (!(err = dns_resolve_record(resolve, &section, &rr, &rdata))) {
        const char *str = dns_rdata_str(&rr, &rdata);

        if (section != current) {
            server_response_print(client, ";; %s\n", dns_section_str(section));
            current = section;
        }

        server_response_print(client, "%-32s %-5s %-10s %s\n", rr.name, dns_class_str(rr.class), dns_type_str(rr.type), str);
/*
        if (section == DNS_AN && rr.type == DNS_CNAME) {
            server_response_print(client, "%s is an alias for %s\n", rr.name, str);
        } else if (section == DNS_AN && rr.type == DNS_A) {
            server_response_print(client, "%s has address %s\n", rr.name, str);
        } else if (section == DNS_AN && rr.type == DNS_AAAA) {
            server_response_print(client, "%s has IPv6 address %s\n", rr.name, str);
        } else if (section == DNS_AN && rr.type == DNS_MX) {
            server_response_print(client, "%s mail is handled by %u %s\n", rr.name, rdata.MX.preference, rdata.MX.exchange);
        }
*/
    }

    dns_close(resolve);

    return 0;
}

int server_dns_request (struct server_handler *handler, struct server_client *client, const char *method, const struct url *url)
{
	struct server_dns *s = (void *) handler;
    const char *name = NULL, *type = NULL;
    int err;

    // read request
    const char *header, *value;
    log_debug("%s", url->query);

    while (!(err = server_request_header(client, &header, &value))) {

    }

    if (err < 0) {
        log_error("server_request_header");
        return err;
    }

    // parse GET/POST parameters
    const char *key;

    while (!(err = server_request_param(client, &key, &value))) {
        if (!strcasecmp(key, "name")) {
            log_debug("name=%s", value);
            name = value;
        } else if (!strcasecmp(key, "type")) {
            log_debug("type=%s", type);
            type = value;
        } else {
            log_debug("%s?", key);
        }
    }

    if (err < 0) {
        log_error("url_decode");
        return err;
    }

    // handle
    return server_dns_lookup(s, client, name, type);
}

int server_dns_create (struct server_dns **sp, struct server *server, const char *path, const char *resolver)
{
	struct server_dns *s;

	if (!(s = calloc(1, sizeof(*s)))) {
		log_perror("calloc");
		return -1;
	}

    s->handler.request = server_dns_request;

    log_info("GET %s", path);

	if (server_add_handler(server, "GET", path, &s->handler)) {
        log_error("server_add_handler: GET");
        goto error;
    }
	
    if (server_add_handler(server, "POST", path, &s->handler)) {
        log_error("server_add_handler: POST");
        goto error;
    }
    
    if (dns_create(s->handler.event_main, &s->dns, resolver)) {
        log_error("dns_create: %s", resolver);
        goto error;
    }

	*sp = s;
	return 0;

error:
    free(s);
    return -1;
}

void server_dns_destroy (struct server_dns *s)
{
	free(s);
}
