#ifndef SERVER_DNS_H
#define SERVER_DNS_H

#include "server/server.h"

struct server_dns;

/*
 * Initialize and mount onto the given server path.
 *
 * resolver:    passed to dns_create(), may be NULL.
 */
int server_dns_create (struct server_dns **sp, struct server *server, const char *path, const char *resolver);

/*
 * Release all associated resources.
 *
 * Only do this after the handler has been unregistered, i.e. server_destroy()!
 */
void server_dns_destroy (struct server_dns *s);

#endif
