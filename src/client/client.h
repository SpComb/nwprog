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
 * Open a client for the given (parsed) URL.
 */
int client_open (struct client *client, const struct url *url);

/*
 * Release any resources used by the client.
 */
void client_destroy (struct client *client);

#endif
