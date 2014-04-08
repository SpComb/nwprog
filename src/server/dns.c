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

int server_dns_lookup (struct server_client *client, const char *name, const char *type)
{
    if (!name)
        return server_response_error(client, 400, NULL, "Missing required <tt>name=...</tt> parameter");

    if (!type)
        type = "A";

    server_response(client, 200, NULL);
    server_response_header(client, "Content-Type", "text/plain");
    server_response_print(client, "%s %s? ...\n", name, type);
}

int server_dns_request (struct server_handler *handler, struct server_client *client, const char *method, const struct url *url)
{
	struct server_dns *s = (void *) handler;
    const char *name = NULL, *type = NULL;
    int err;

    // read request
    const char *header, *value;
    log_info("%s", url->query);

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
            log_info("name=%s", value);
            name = value;
        } else if (!strcasecmp(key, "type")) {
            log_info("type=%s", type);
            type = value;
        } else {
            log_info("%s?", key);
        }
    }

    if (err < 0) {
        log_error("url_decode");
        return err;
    }

    // handle
    return server_dns_lookup(client, name, type);
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
