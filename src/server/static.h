#ifndef SERVER_STATIC_H
#define SERVER_STATIC_H

#include "server/server.h"

struct server_static;

/*
 * Initialize.
 */
int server_static_create (struct server_static **sp, const char *root);

/*
 * Mount onto the given server path.
 */
int server_static_add (struct server_static *s, struct server *server, const char *path);

/*
 * Release all associated resources.
 *
 * Only do this after the handler has been unregistered, i.e. server_destroy()!
 */
void server_static_destroy (struct server_static *s);

#endif
