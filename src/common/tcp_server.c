#include "common/tcp.h"

#include "common/log.h"
#include "common/sock.h"
#include "common/stream.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

struct tcp_server {
	int sock;
	
	struct event_main *event_main;
	struct event *event;

	tcp_server_handler *func;
	void *ctx;
};

void tcp_server_event (struct event *event, int flags, void *ctx)
{
	struct tcp_server *server = ctx;
	struct tcp_stream *stream;

	// accept
	int sock;
	struct sockaddr_storage addr;
	socklen_t addrlen = sizeof(addr);
	
	// XXX: fatal?
	if ((sock = accept(server->sock, (struct sockaddr *) &addr, &addrlen)) < 0) {
		log_perror("accept");
		return;
	}

	log_info("%s accept %s", sockname_str(sock), sockpeer_str(sock));

	if (tcp_stream_create(server->event_main, &stream, sock)) {
		log_error("tcp_stream_create");
		return;
	}

	server->func(server, stream, server->ctx);
}

int tcp_server (struct event_main *event_main, struct tcp_server **serverp, const char *host, const char *port, tcp_server_handler *func, void *ctx)
{
	struct tcp_server *server;
	int err;

	if (!(server = calloc(1, sizeof(*server)))) {
		log_perror("calloc");
		return -1;
	}

	server->event_main = event_main;
	server->func = func;
	server->ctx = ctx;
	
	if ((err = tcp_listen(&server->sock, host, port, TCP_LISTEN_BACKLOG))) {
		log_perror("tcp_listen %s:%s", host, port);
		goto error;
	}

	if ((err = sock_nonblocking(server->sock))) {
		log_error("sock_nonblocking");
		goto error;
	}

	if ((err = event_create(event_main, &server->event, server->sock, tcp_server_event, server))) {
		log_error("event_create");
		goto error;
	}

	if ((err = event_set(server->event, EVENT_READ))) {
		log_error("event_set");
		goto error;
	}

	// ok
	*serverp = server;
	return 0;

error:
	free(server);
	return err;
}


