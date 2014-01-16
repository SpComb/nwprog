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
 * Write response data to FILE, or NULL to bitbucket.
 *
 * The given FILE is owned by the client, and will be closed...
 */
int client_set_response_file (struct client *client, FILE *file);

/*
 * Add a custom header to requests
 */
int client_add_header (struct client *client, const char *header, const char *value);

/*
 * Open a client for the given URL scheme://host:port.
 */
int client_open (struct client *client, const struct url *url);

/*
 * Perform a GET request for the given URL /path.
 */
int client_get (struct client *client, const struct url *url);

/*
 * Perform a PUTT request for the given file and URL /path.
 */
int client_put (struct client *client, const struct url *url, FILE *file);

/*
 * Release any resources used by the client.
 */
void client_destroy (struct client *client);

#endif
