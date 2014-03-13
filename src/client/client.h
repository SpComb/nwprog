#ifndef CLIENT_H
#define CLIENT_H

#include "common/event.h"
#include "common/http.h"
#include "common/url.h"

#ifdef WITH_SSL
#   include "common/ssl.h"
#endif

#include <stdbool.h>

/*
 * HTTP Client.
 */
struct client;

/*
 * Create a new client.
 */
int client_create (struct event_main *event_main, struct client **clientp);

#ifdef WITH_SSL
/*
 * Set SSL context for https support.
 */
int client_set_ssl (struct client *client, struct ssl_main *ssl_main);
#endif

/*
 * Write response data to FILE, or NULL to bitbucket.
 *
 *  close:   client will close FILE after use.
 */
int client_set_response_file (struct client *client, FILE *file, bool close);

/*
 * Change HTTP version to use for requests.
 *
 * Using HTTP_11 enables connection persistence.
 */
int client_set_request_version (struct client *client, enum http_version version);

/*
 * Add a custom header to requests
 */
int client_add_header (struct client *client, const char *header, const char *value);

/*
 * Open a client for the given scheme://host:port.
 *
 * The connection will be used for any subsequent GET/PUT request.
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
 * Close any open connection.
 */
int client_close (struct client *client);

/*
 * Release any resources used by the client.
 */
void client_destroy (struct client *client);

#endif
