#ifndef SERVER_H
#define SERVER_H

#include "common/http.h"

#include <stdio.h>

/*
 * HTTP Server.
 */
struct server;
struct server_client;

/*
 * Request handler.
 */
struct server_handler {
	int (*request)(struct server_handler *handler, struct server_client *client, const char *method, const char *path);

/*
	int (*request_header)(struct server_handler *handler, struct server_client *client, const char *name, const char *value);
	int (*request_body)(struct server_handler *handler, struct server_client *client, size_t content_length, FILE **filep);

	int (*response)(struct server_handler *handler, struct server_client *client);
*/
};

/*
 * Initialize a new server that listens on the given host/port.
 */
int server_create (struct server **serverp, const char *host, const char *port);

/*
 * Add a server handler for requests.
 */
int server_add_handler (struct server *server, const char *method, const char *path, struct server_handler *handler);

/*
 * Read request header.
 *
 * Returns 1 on end-of-headers.
 */
int server_request_header (struct server_client *client, const char **name, const char **value);

/*
 * Read request body from client into FILE.
 *
 * Returns 1 if there was no request body.
 */
int server_request_file (struct server_client *client, FILE *file);

/*
 * Send response to client request.
 */
int server_response (struct server_client *client, enum http_status status, const char *reason);

/*
 * Send response header.
 */
int server_response_header (struct server_client *client, const char *name, const char *fmt, ...)
	__attribute((format (printf, 3, 4)));

/*
 * Send response body from file.
 */
int server_response_file (struct server_client *client, size_t content_length, FILE *file);

/*
 * Process client requests.
 */
int server_run (struct server *server);

/*
 * Release all resources for server.
 */
void server_destroy (struct server *server);

#endif
