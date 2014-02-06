#ifndef CLIENT_H
#define CLIENT_H

#include "common/url.h"

#ifdef WITH_SSL
#   include "common/ssl.h"
#endif

/*
 * HTTP Client.
 */
struct client;

/*
 * Create a new client.
 */
int client_create (struct client **clientp);

#ifdef WITH_SSL
/*
 * Set SSL context for https support.
 */
int client_set_ssl (struct client *client, struct ssl_main *ssl_main);
#endif

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
 * Open a client for the given scheme://host:port.
 */
int client_open (struct client *client, const struct url *url);

/*
 * Perform a GET request for the given URL /path.
 *
 * Returns HTTP response status, or <0 on error.
 */
int client_get (struct client *client, const struct url *url);

/*
 * Perform a PUT request for the given file and URL /path.
 *
 * Returns HTTP response status, or <0 on error.
 */
int client_put (struct client *client, const struct url *url, FILE *file);

/*
 * Release any resources used by the client.
 */
void client_destroy (struct client *client);

#endif
