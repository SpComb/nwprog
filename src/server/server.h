#ifndef SERVER_H
#define SERVER_H

/*
 * HTTP Server.
 */
struct server;

/*
 * Initialize a new server that listens on the given host/port.
 */
int server_create (struct server **serverp, const char *host, const char *port);

int server_run (struct server *server);

/*
 * Release all resources for server.
 */
void server_destroy (struct server *server);

#endif
