#include "common/tcp.h"

#include "common/log.h"
#include "common/sock.h"
#include "common/stream.h"

#include <unistd.h>

struct tcp {
	int sock;
	
	struct event *event;

	struct stream *read, *write;
};

void tcp_event (struct event *event, int flags, void *ctx)
{
	log_info("flags=%#02x", flags);
}

int tcp_create (struct event_main *event_main, struct tcp **tcpp, int sock)
{
	struct tcp *tcp;

	if (!(tcp = calloc(1, sizeof(*tcp)))) {
		log_perror("calloc");
		return -1;
	}

	tcp->sock = sock;

	if (event_create(event_main, &tcp->event, sock)) {
		log_warning("event_create");
		goto error;
	}

	if (stream_create(&tcp->read, sock, TCP_STREAM_SIZE)) {
		log_warning("stream_create read");
		goto error;
	}
	
	if (stream_create(&tcp->write, sock, TCP_STREAM_SIZE)) {
		log_warning("stream_create write");
		goto error;
	}

	*tcpp = tcp;

	return 0;
error:
	tcp_destroy(tcp);
	return -1;
}

struct stream * tcp_read_stream (struct tcp *tcp)
{
    return tcp->read;
}

struct stream * tcp_write_stream (struct tcp *tcp)
{
    return tcp->write;
}

const char * tcp_sock_str (struct tcp *tcp)
{
	return sockname_str(tcp->sock);
}

const char * tcp_peer_str (struct tcp *tcp)
{
	return sockpeer_str(tcp->sock);
}

void tcp_destroy (struct tcp *tcp)
{
    if (tcp->event)
        event_destroy(tcp->event);

	if (tcp->write)
		stream_destroy(tcp->write);

	if (tcp->read)
		stream_destroy(tcp->read);

    if (tcp->sock >= 0)
        close(tcp->sock);

	free(tcp);
}
