#include "server/server.h"

#include "common/http.h"
#include "common/log.h"
#include "common/sock.h"
#include "common/tcp.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <unistd.h>

struct server {
    struct event_main *event_main;

	TAILQ_HEAD(server_handlers, server_handler_item) handlers;
};

struct server_handler_item {
	const char *method;
	const char *path;

	struct server_handler *handler;

	TAILQ_ENTRY(server_handler_item) server_handlers;
};

struct server_client {
	struct http *http;

	/* Request */
	char request_method[HTTP_METHOD_MAX];
	char request_path[HTTP_PATH_MAX];
	
	size_t request_content_length;
	
	/* Sent */
	unsigned status;
	bool header;
	bool headers;
	bool body;

	int err;
};

/*
 * Handle client request.
 */
void server_client (struct tcp_server *tcp_server, struct tcp_stream *stream, void *ctx);

int server_create (struct event_main *event_main, struct server **serverp)
{
	struct server *server = NULL;

	if (!(server = calloc(1, sizeof(*server)))) {
		log_perror("calloc");
		return -1;
	}

	TAILQ_INIT(&server->handlers);

    server->event_main = event_main;

	*serverp = server;

	return 0;
}

int server_listen (struct server *server, const char *host, const char *port)
{
    struct tcp_server *tcp;

    if (tcp_server(server->event_main, &tcp, host, port, server_client, server)) {
        log_warning("tcp_server");
        return -1;
    }

    // XXX; should we perhaps store tcp_server somewhere?
    return 0;
}

int server_add_handler (struct server *server, const char *method, const char *path, struct server_handler *handler)
{
	struct server_handler_item *h = NULL;

	if (!(h = calloc(1, sizeof(*h)))) {
		log_pwarning("calloc");
		return -1;
	}
	
	h->method = method;
	h->path = path;
	h->handler = handler;

	TAILQ_INSERT_TAIL(&server->handlers, h, server_handlers);

	return 0;
}

/*
 * Lookup a handler for the given request.
 */
int server_lookup_handler (struct server *server, const char *method, const char *path, struct server_handler **handlerp)
{
	struct server_handler_item *h;

	TAILQ_FOREACH(h, &server->handlers, server_handlers) {
		if (h->method && strcmp(h->method, method))
			continue;

		if (h->path && strncmp(h->path, path, strlen(h->path)))
			continue;
		
		log_debug("%s", h->path);

		*handlerp = h->handler;
		return 0;
	}
	
	log_warning("%s: not found", path);
	return 404;
}

int server_request (struct server_client *client)
{
	const char *method, *path, *version;
	int err;

	if ((err = http_read_request(client->http, &method, &path, &version))) {
		log_warning("http_read_request");
		return err;
	}

	if (strlen(method) >= sizeof(client->request_method)) {
		log_warning("method is too long: %zu", strlen(method));
		return 400;
	} else {
		strncpy(client->request_method, method, sizeof(client->request_method));
	}

	if (strlen(path) >= sizeof(client->request_path)) {
		log_warning("path is too long: %zu", strlen(path));
		return 400;
	} else {
		strncpy(client->request_path, path, sizeof(client->request_path));
	}

	log_info("%s %s %s", method, path, version);

	return 0;
}

int server_request_header (struct server_client *client, const char **namep, const char **valuep)
{
	int err;

	if ((err = http_read_header(client->http, namep, valuep)) < 0) {
		log_warning("http_read_header");
		return err;
	}

	if (err)
		return 1;

	log_info("\t%20s : %s", *namep, *valuep);

	if (strcasecmp(*namep, "Content-Length") == 0) {
		if (sscanf(*valuep, "%zu", &client->request_content_length) != 1) {
			log_warning("invalid content_length: %s", *valuep);
			return 400;
		}

		log_debug("content_length=%zu", client->request_content_length);
	}
	
	return 0;
}

int server_request_file (struct server_client *client, FILE *file)
{
	int err;

	// TODO: Transfer-Encoding?
	if (!client->request_content_length) {
		log_debug("no request body given");
		return 411;
	}
		
	if (((err = http_read_file(client->http, file, client->request_content_length)))){
		log_warning("http_read_file");
		return err;
	}
	
	return 0;
}

int server_response (struct server_client *client, enum http_status status, const char *reason)
{
	if (client->status) {
		log_fatal("attempting to re-send status: %u", status);
		return -1;
	}

	log_info("%u %s", status, reason);

	client->status = status;

	if (http_write_response(client->http, status, reason)) {
		log_error("failed to write response line");
		return -1;
	}
	
	return 0;
}

int server_response_header (struct server_client *client, const char *name, const char *fmt, ...)
{
	int err;

	if (!client->status) {
		log_fatal("attempting to send headers without status: %s", name);
		return -1;
	}

	if (client->headers) {
		log_fatal("attempting to re-send headers");
		return -1;
	}

	va_list args;

	log_info("\t%20s : %s", name, fmt);

	client->header = true;

	va_start(args, fmt);
	err = http_write_headerv(client->http, name, fmt, args);
	va_end(args);
	
	if (err) {
		log_error("failed to write response header line");
		return -1;
	}

	return 0;
}

int server_response_headers (struct server_client *client)
{
	client->headers = true;

	if (http_write_headers(client->http)) {
		log_error("failed to write end-of-headers");
		return -1;
	}

	return 0;
}

int server_response_file (struct server_client *client, size_t content_length, FILE *file)
{
	int err;

	if ((err = server_response_header(client, "Content-Length", "%zu", content_length))) {
		return err;
	}
	
	// headers
	if ((err = server_response_headers(client))) {
		return err;
	}

	// body
	if (client->body) {
		log_fatal("attempting to re-send body");
		return -1;
	}

	client->body = true;

	if (http_write_file(client->http, file, content_length)) {
		log_error("failed to write response body");
		return -1;
	}

	return 0;
}

int server_response_print (struct server_client *client, const char *fmt, ...)
{
	va_list args;
	int err = 0;

	if (!client->status) {
		log_fatal("attempting to send response body without status");
		return -1;
	}

	if (!client->headers) {
		err |= server_response_header(client, "Connection", "close");
		err |= server_response_headers(client);
	}

	if (err)
		return err;
	
	// body
	client->body = true;

	va_start(args, fmt);
	err = http_vwrite(client->http, fmt, args);
	va_end(args);

	if (err) {
		log_warning("http_vwrite");
		return err;
	}

	return 0;
}

int server_client_request (struct server *server, struct server_client *client)
{
	struct server_handler *handler = NULL;
	enum http_status status = 0;
	int err;

	// request
	if ((err = server_request(client))) {
		goto error;
	}

	// handler 
	if ((err = server_lookup_handler(server, client->request_method, client->request_path, &handler)) < 0) {
		goto error;

	} else if (err) {
		handler = NULL;

	} else {
		err = handler->request(handler, client, client->request_method, client->request_path);
	}

error:	
	// response
	if (err < 0) {
		status = HTTP_INTERNAL_SERVER_ERROR;

	} else if (err > 0) {
		status = err;

	} else if (client->status) {
		status = 0;

	} else {
		log_warning("status not sent, defaulting to 500");
		status = 500;
	}
	
	if (status && client->status) {
		log_warning("status %u already sent, should be %u", client->status, status);

	} else if (status) {
		if (server_response(client, status, NULL)) {
			log_warning("failed to send response status");
			err = -1;
		}
	}
	
	// headers
	if (!client->headers) {
		if (server_response_headers(client)) {
			log_warning("failed to end response headers");
			err = -1;
		}
	}

	// TODO: body on errors
	return err;
}

// TODO: multiple requests
void server_client (struct tcp_server *tcp_server, struct tcp_stream *stream, void *ctx)
{
    struct server *server = ctx;
	struct server_client client = { };
    int err = 0;

	// http
	if ((err = http_create(&client.http, tcp_stream_read(stream), tcp_stream_write(stream)))) {
		log_perror("http_create");
		goto error;
	}

    if ((err = server_client_request(server, &client))) {
        log_warning("server_client_request");
        goto error;
    }

error:
	if (client.http)
		http_destroy(client.http);
    
    tcp_stream_destroy(stream);
}

void server_destroy (struct server *server)
{
	// handlers
    struct server_handler_item *h;

    while ((h = TAILQ_FIRST(&server->handlers))) {
        TAILQ_REMOVE(&server->handlers, h, server_handlers);
        free(h);
    }

	free(server);
}
