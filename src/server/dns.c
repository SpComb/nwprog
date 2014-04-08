#include "server/dns.h"

#include "common/log.h"
#include "../dns.h" // XXX: terrible naming failure

struct server_dns {
	/* Embed */
	struct server_handler handler;

    /* Shared across requests */
    struct dns *dns;
};

int server_dns_request (struct server_handler *handler, struct server_client *client, const char *method, const struct url *url)
{
	struct server_dns *s = (void *) handler;

    return 400;
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
        log_error("server_add_handler");
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
