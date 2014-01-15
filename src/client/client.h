#ifndef CLIENT_H
#define CLIENT_H

#include "common/url.h"

/*
 * HTTP Client.
 */
struct client;

/*
 * Create a new client.
 */
int client_create (struct client **clientp);

/*
 * Open a client for the given URL scheme://host:port.
 */
int client_open (struct client *client, const struct url *url);

/*
 * Perform a GET request for the given URL /path.
 */
int client_get (struct client *client, const struct url *url);

/*
 * Release any resources used by the client.
 */
void client_destroy (struct client *client);

#endif
