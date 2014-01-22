#ifndef SERVER_STATIC_H
#define SERVER_STATIC_H

#include "server/server.h"

struct server_static;

enum server_static_flags {
    SERVER_STATIC_GET       = 0x01,
    SERVER_STATIC_PUT       = 0x02,
};

/*
 * Initialize and mount onto the given server path.
 */
int server_static_create (struct server_static **sp, const char *root, struct server *server, const char *path, int flags);

/*
 * Release all associated resources.
 *
 * Only do this after the handler has been unregistered, i.e. server_destroy()!
 */
void server_static_destroy (struct server_static *s);

#endif
