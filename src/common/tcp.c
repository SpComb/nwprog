#include "common/tcp.h"
#include "common/tcp_internal.h"

#include "common/log.h"
#include "common/sock.h"
#include "common/stream.h"

#include <unistd.h>

int tcp_stream_read (char *buf, size_t *sizep, void *ctx)
{
    struct tcp *tcp = ctx;
    int err;
    
    const struct timeval *timeout = \
        (tcp->read_timeout.tv_sec || tcp->read_timeout.tv_usec) ? &tcp->read_timeout : NULL;

    while ((err = sock_read(tcp->sock, buf, sizep)) > 0 && tcp->event) {
        if ((err = event_yield(tcp->event, EVENT_READ, timeout))) {
            log_error("event_yield");
            return err;
        }
    }

    if (err) {
        log_error("sock_read");
        return -1;
    }

    if (!*sizep) {
        log_debug("eof");
        return 1;
    }

    return 0;
}

int tcp_stream_write (const char *buf, size_t *sizep, void *ctx)
{
    struct tcp *tcp = ctx;
    int err;

    while ((err = sock_write(tcp->sock, buf, sizep)) > 0 && tcp->event) {
        if (event_yield(tcp->event, EVENT_WRITE, NULL)) {
            log_error("event_write");
            return -1;
        }
    }

    if (err) {
        log_error("sock_write");
        return -1;
    }

    if (!*sizep) {
        log_debug("eof");
        return 1;
    }

    return 0;
}

int tcp_stream_sendfile (int fd, size_t *sizep, void *ctx)
{
    struct tcp *tcp = ctx;
    int err;

    if (!*sizep) {
        // XXX: choose a random default
        *sizep = TCP_STREAM_SIZE;
    }

    while ((err = sock_sendfile(tcp->sock, fd, sizep)) > 0 && tcp->event) {
        if (event_yield(tcp->event, EVENT_WRITE, NULL)) {
            log_error("event_write");
            return -1;
        }
    }

    if (err) {
        log_error("sock_write");
        return -1;
    }

    if (!*sizep) {
        log_debug("eof");
        return 1;
    }

    return 0;

}

static const struct stream_type tcp_stream_type = {
    .read       = tcp_stream_read,
    .write      = tcp_stream_write,
    .sendfile   = tcp_stream_sendfile,
};

int tcp_create (struct event_main *event_main, struct tcp **tcpp, int sock)
{
	struct tcp *tcp;

	if (!(tcp = calloc(1, sizeof(*tcp)))) {
		log_perror("calloc");
		return -1;
	}

	tcp->sock = sock;
    
    if (event_main) {
        if (sock_nonblocking(sock)) {
            log_error("sock_nonblocking");
            goto error;
        }

        if (event_create(event_main, &tcp->event, sock)) {
            log_error("event_create");
            goto error;
        }
    }

	if (stream_create(&tcp_stream_type, &tcp->read, TCP_STREAM_SIZE, tcp)) {
		log_error("stream_create read");
		goto error;
	}
	
	if (stream_create(&tcp_stream_type, &tcp->write, TCP_STREAM_SIZE, tcp)) {
		log_error("stream_create write");
		goto error;
	}

	*tcpp = tcp;

	return 0;
error:
	tcp_destroy(tcp);
	return -1;
}

int tcp_sock (struct tcp *tcp)
{
    return tcp->sock;
}

struct stream * tcp_read_stream (struct tcp *tcp)
{
    return tcp->read;
}

struct stream * tcp_write_stream (struct tcp *tcp)
{
    return tcp->write;
}

void tcp_read_timeout (struct tcp *tcp, const struct timeval *timeout)
{
    tcp->read_timeout = *timeout;
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
