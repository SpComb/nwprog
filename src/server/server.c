#include "server/server.h"

#include "common/http.h"
#include "common/log.h"
#include "common/sock.h"
#include "common/tcp.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct server {
	int sock;
};

struct server_request {
	const char *method;
	const char *path;
	const char *version;

	/* Headers */
	size_t content_length;
};

struct server_response {
	unsigned status;
	const char *reason;

	/* Headers */
	size_t content_length;

	/* Write response content to FILE */
	FILE *content_file;
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

	*serverp = server;

	return 0;
error:
	free(server);

	return -1;
}

/*
 * Read in request
 */
int server_request (struct server *server, struct http *http, struct server_request *request)
{
	int err;

	if (http_read_request(http, &request->method, &request->path, &request->version)) {
		log_error("http_read_request");
		return 1;
	}

	log_info("%s %s %s", request->method, request->path, request->version);

	// headers
	const char *header, *value;

	while (!(err = http_read_header(http, &header, &value))) {
		log_info("\t%20s : %s", header, value);

		if (strcasecmp(header, "Content-Length") == 0) {
			if (sscanf(value, "%zu", &request->content_length) != 1) {
				log_warning("invalid content_length: %s", value);
				return 1;
			}

			log_debug("content_length=%zu", request->content_length);
		}
	}

	if (err < 0) {
		log_error("http_read_header");
		return 1;
	}
	
	// determine if the request includes a body
	// TODO: Transfer-Encoding
	if (!request->content_length) {
		log_debug("not expecting a request body");

	} else {
	   	if (((err = http_read_file(http, NULL, request->content_length)))){
			log_error("http_read_file");
			return 1;
		}
	}

	return 0;
}

/*
 * Send response.
 */
int server_response (struct server *server, struct http *http, struct server_response *response)
{
	log_info("%u %s", response->status, response->reason);

	if (http_write_response(http, response->status, response->reason)) {
		log_error("failed to write response line");
		return 1;
	}

	if (response->content_length) {
		log_info("\t%20s : %zu", "Content-Length", response->content_length);

	   	if (http_write_header(http, "Content-Length", "%zu", response->content_length)) {
			log_error("failed to write response header line");
			return 1;
		}
	}

	if (http_write_headers(http)) {
		log_error("failed to write end-of-headers");
		return 1;
	}

	if (response->content_file && http_write_file(http, response->content_file, response->content_length)) {
		log_error("failed to write response body");
		return 1;
	}

	return 0;
}

int server_run (struct server *server)
{
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
	struct http *http;

	if ((err = http_create(&http, sock))) {
		log_perror("http_create %s", sockpeer_str(sock));
		goto error;
	}

	struct server_request request = { };

	if ((err = server_request(server, http, &request))) {
		goto error;
	}
	
	struct server_response response = {
		.status		= 200,
		.reason		= "OK",
	};

	if ((err = server_response(server, http, &response))) {
		goto error;
	}

	// ok

error:
	http_destroy(http);
	return err;
}

void server_destroy (struct server *server)
{
	if (server->sock >= 0)
		close(server->sock);

	free(server);
}
