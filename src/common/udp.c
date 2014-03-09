#include "common/udp.h"

#include "common/log.h"
#include "common/sock.h"

#include <netdb.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

struct udp {
    int sock;

    struct event *event;
};

/*
 * Create a new udp owning the given sock.
 *
 * Closes sock on error.
 */
int udp_create (struct event_main *event_main, struct udp **udpp, int sock)
{
    struct udp *udp;

    if (!(udp = calloc(1, sizeof(*udp)))) {
        log_perror("calloc");
        return -1;
    }

    udp->sock = sock;

    if (event_main) {
        if (sock_nonblocking(sock)) {
            log_error("sock_nonblocking");
            goto error;
        }

        if (event_create(event_main, &udp->event, sock)) {
            log_error("event_create");
            goto error;
        }
    }

    *udpp = udp;

    return 0;

error:
    close(sock);
    free(udp);

    return -1;
}

int udp_connect (struct event_main *event_main, struct udp **udpp, const char *host, const char *port)
{
	int err;
	struct addrinfo hints = {
		.ai_flags		= 0,
		.ai_family		= AF_UNSPEC,
		.ai_socktype	= SOCK_DGRAM,
		.ai_protocol	= 0,
	};
	struct addrinfo *addrs, *addr;
	int sock = -1;

	if ((err = getaddrinfo(host, port, &hints, &addrs))) {
		log_perror("getaddrinfo %s:%s: %s", host, port, gai_strerror(err));
		return -1;
	}

	for (addr = addrs; addr; addr = addr->ai_next) {
		if ((sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol)) < 0) {
			log_pwarning("socket(%d, %d, %d)", addr->ai_family, addr->ai_socktype, addr->ai_protocol);
			continue;
		}

		log_info("%s...", sockaddr_str(addr->ai_addr, addr->ai_addrlen));

		if ((err = connect(sock, addr->ai_addr, addr->ai_addrlen)) < 0) {
			log_pwarning("connect");
			close(sock);
			sock = -1;
			continue;
		}

		log_info("%s <- %s", sockpeer_str(sock), sockname_str(sock));

		break;
	}

	freeaddrinfo(addrs);

    if (sock < 0)
        return -1;

    // create
    if ((err = udp_create(event_main, udpp, sock))) {
        log_error("udp_create: %d", sock);
        return -1;
    }

    return 0;
}

int udp_read (struct udp *udp, void *buf, size_t *sizep, const struct timeval *timeout)
{
    int err;
    
    log_debug("%zu..", *sizep);
    
    while ((err = sock_read(udp->sock, buf, sizep)) > 0 && udp->event) {
        if ((err = event_yield(udp->event, EVENT_READ, timeout)) < 0) {
            log_error("event_yield");
            return err;
        }

        if (err) {
            log_debug("timeout");
            return 1;
        }
    }

    if (err) {
        log_error("sock_read");
        return -1;
    }

    log_debug("%zu", *sizep);

    return 0;
}

int udp_write (struct udp *udp, void *buf, size_t size)
{
    int err;

    if ((err = sock_write(udp->sock, buf, &size))) {
        log_error("sock_write");
        return err;
    }
    
    // we assume that write() will be atomically for SOCK_DGRAM.
    log_debug("%zu", size);

    return 0;
}

struct event * udp_event (struct udp *udp)
{
    return udp->event;
}

void udp_destroy (struct udp *udp)
{
    if (close(udp->sock))
        log_pwarning("close");

    free(udp);
}
