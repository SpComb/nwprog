#include "server/server.h"

#include "common/http.h"
#include "common/log.h"
#include "common/sock.h"
#include "common/tcp.h"

#include <stdlib.h>
#include <unistd.h>

struct server {
	int sock;
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

int server_request (struct server *server, struct http *http)
{
	int err;

	const char *method, *path, *version;

	if (http_read_request(http, &method, &path, &version)) {
		log_error("http_read_request");
		return 1;
	}

	log_info("%s %s %s", method, path, version);

	// headers
	const char *name, *value;

	while (!(err = http_read_header(http, &name, &value))) {
		log_info("\t%20s : %s", name, value);
	}

	if (err < 0) {
		log_error("http_read_header");
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

	if ((err = server_request(server, http))) {
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
