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
	int sock;

	TAILQ_HEAD(server_handlers, server_handler_item) handlers;
};

struct server_handler_item {
	const char *method;
	const char *path;

	struct server_handler *handler;

	TAILQ_ENTRY(server_handler_item) server_handlers;
};

struct server_request {
	/* Headers */
	size_t content_length;
};

struct server_response {
	/* Headers */
	size_t content_length;

	/* Write response content to FILE */
	FILE *content_file;
};

struct server_client {
	struct http *http;
	
	/* Sent */
	unsigned status;
	bool header;
	bool headers;
	bool body;

	int err;
};

int server_create (struct server **serverp, const char *host, const char *port)
{
	struct server *server = NULL;

	if (!(server = calloc(1, sizeof(*server)))) {
		log_perror("calloc");
		return -1;
	}

	if ((server->sock = tcp_listen(host, port, TCP_LISTEN_BACKLOG)) < 0) {
		log_perror("tcp_listen %s:%s", host, port);
		goto error;
	}

	TAILQ_INIT(&server->handlers);

	*serverp = server;

	return 0;
error:
	free(server);

	return -1;
}

int server_add_handler (struct server *server, const char *method, const char *path, struct server_handler *handler)
{
	struct server_handler_item *h = NULL;

	if (!(h = calloc(1, sizeof(h)))) {
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
		
		log_info("%s", h->path);

		*handlerp = h->handler;
		return 0;
	}
	
	log_info("%s: not found", path);
	return 1;
}

int server_client_response (struct server_client *client, enum http_status status, const char *reason)
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

int server_client_header (struct server_client *client, const char *name, const char *fmt, ...)
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

int server_client_headers (struct server_client *client)
{
	client->headers = true;

	if (http_write_headers(client->http)) {
		log_error("failed to write end-of-headers");
		return -1;
	}

	return 0;
}

int server_client_file (struct server_client *client, size_t content_length, FILE *file)
{
	int err;

	if ((err = server_client_header(client, "Content-Length", "%zu", content_length))) {
		return err;
	}
	
	// headers
	if ((err = server_client_headers(client))) {
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

/*
 * Handle client request.
 */
int server_client (struct server *server, struct server_client *client)
{
	struct server_request request = { };
	struct server_handler *handler = NULL;
	int err;

	// request
	{
		const char *method, *path, *version;

		if ((err = http_read_request(client->http, &method, &path, &version))) {
			log_warning("http_read_request");
			goto error;
		}

		log_info("%s %s %s", method, path, version);

		if ((err = server_lookup_handler(server, method, path, &handler)) < 0)
			goto error;
		
		// handle
		if (handler && (err = handler->request(handler, client, method, path))) {
			goto error;
		}
	}

	// headers
	{
		const char *header, *value;

		while (!(err = http_read_header(client->http, &header, &value))) {
			log_info("\t%20s : %s", header, value);

			if (strcasecmp(header, "Content-Length") == 0) {
				if (sscanf(value, "%zu", &request.content_length) != 1) {
					log_warning("invalid content_length: %s", value);
					err = 1;
					goto error;
				}

				log_debug("content_length=%zu", request.content_length);
			}

			if (handler && ((err = handler->request_header(handler, client, header, value)))) {
				goto error;
			}
		}

		if (err < 0) {
			log_warning("http_read_header");
			goto error;
		}

		err = 0;
	}
	
	// body
	{
		// TODO: determine if the request includes a body; inspect Transfer-Encoding?
		if (!request.content_length) {
			log_debug("not expecting a request body");

		} else {
			FILE *file = NULL;
			
			if (handler && ((err = handler->request_body(handler, client, request.content_length, &file)))) {
				goto error;
			}

			if (((err = http_read_file(client->http, file, request.content_length)))){
				log_warning("http_read_file");
				goto error;
			}
		}
	}

	// respond
	if (handler && (err = handler->response(handler, client))) {
		goto error;
	}

error:	
	{
		// status
		enum http_status status = 0;

		if (err < 0) {
			status = HTTP_INTERNAL_SERVER_ERROR;

		} else if (err > 0) {
			status = HTTP_BAD_REQUEST;

		} else if (!handler) {
			status = HTTP_NOT_FOUND;

		} else {
			status = 0;
		}
		
		if (status && client->status) {
			log_warning("status %u already sent, should be %u", client->status, status);
		} else if (!status && !client->status) {
			log_warning("status not sent");
		} else {
			if (server_client_response(client, status, NULL)) {
				log_warning("failed to send response status");
				err = -1;
			}
		}
		
		// headers
		if (!client->headers) {
			if (server_client_headers(client)) {
				log_warning("failed to end response headers");
				err = -1;
			}
		}

		// TODO: body
	}

	return err;
}

int server_run (struct server *server)
{
	struct server_client client = { };
	int err;

	// accept
	// XXX: move this to sock?
	int sock;
	struct sockaddr_storage addr;
	socklen_t addrlen = sizeof(addr);
	
	if ((sock = accept(server->sock, (struct sockaddr *) &addr, &addrlen)) < 0) {
		log_perror("accept");
		return -1;
	}

	log_info("%s accept %s", sockname_str(sock), sockpeer_str(sock));
	
	// http
	if ((err = http_create(&client.http, sock))) {
		log_perror("http_create %s", sockpeer_str(sock));
		sock = -1;
		goto error;
	}
	
	// process
	if ((err = server_client(server, &client))) {
		goto error;
	}

	// ok

error:
	if (client.http)
		http_destroy(client.http);

	if (sock >= 0)
		close(sock);

	return err;
}

void server_destroy (struct server *server)
{
	if (server->sock >= 0)
		close(server->sock);

	free(server);
}
