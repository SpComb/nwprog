#include "common/tcp.h"

#include "common/log.h"
#include "common/sock.h"
#include "common/stream.h"

struct tcp_stream {
	int sock;
	
	struct event *event;

	struct stream *read, *write;
};

void tcp_stream_event (struct event *event, int flags, void *ctx)
{
	log_info("flags=%#02x", flags);
}

int tcp_stream_create (struct event_main *event_main, struct tcp_stream **streamp, int sock)
{
	struct tcp_stream *stream;

	if (!(stream = calloc(1, sizeof(*stream)))) {
		log_perror("calloc");
		return -1;
	}

	stream->sock = sock;
/*
	if (event_create(server->event_main, &stream->event, sock, tcp_stream_event, stream)) {
		log_warning("event_create");
		goto error;
	}
*/
	if (stream_create(&stream->read, sock, TCP_STREAM_SIZE)) {
		log_warning("stream_create read");
		goto error;
	}
	
	if (stream_create(&stream->write, sock, TCP_STREAM_SIZE)) {
		log_warning("stream_create write");
		goto error;
	}
/*
	if (event_set(stream->event, EVENT_READ)) {
		log_warning("event_set");
		goto error;
	}
*/

	*streamp = stream;

	return 0;
error:
	tcp_stream_destroy(stream);
	return -1;
}

struct stream * tcp_stream_read (struct tcp_stream *stream)
{
    return stream->read;
}

struct stream * tcp_stream_write (struct tcp_stream *stream)
{
    return stream->write;
}

const char * tcp_stream_sock_str (struct tcp_stream *stream)
{
	return sockname_str(stream->sock);
}

const char * tcp_stream_peer_str (struct tcp_stream *stream)
{
	return sockpeer_str(stream->sock);
}

void tcp_stream_destroy (struct tcp_stream *stream)
{
	if (stream->write)
		stream_destroy(stream->write);

	if (stream->read)
		stream_destroy(stream->read);

	free(stream);
}
