#include "common/tcp.h"
#include "common/tcp_internal.h"

#include "common/log.h"
#include "common/sock.h"

#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

struct tcp_client {
    struct event_main *event_main;
};

/*
 * Async connect to given addr.
 */
int tcp_client_connect (struct tcp_client *client, int *sockp, struct addrinfo *addr)
{
    struct event *event = NULL;
    int sock;
    int err;

    if ((sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol)) < 0) {
        log_pwarning("socket(%d, %d, %d)", addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        return 1;
    }

    if (client->event_main) {
        if ((err = sock_nonblocking(sock))) {
            log_warning("sock_nonblocking");
            return err;
        }

        if ((err = event_create(client->event_main, &event, sock))) {
            log_warning("event_create");
            return err;
        }
    }

    log_info("%s...", sockaddr_str(addr->ai_addr, addr->ai_addrlen));

    err = sock_connect(sock, addr->ai_addr, addr->ai_addrlen);
    
    if (err > 0 && event) {
        // TODO: timeout
        if (event_yield(event, EVENT_WRITE, NULL)) {
            log_error("event_yield");
            goto error;
        }

        err = sock_error(sock);
    }

    if (err) {
        log_pwarning("sock_connect");
        goto error;
    }

    log_info("%s <- %s", sockpeer_str(sock), sockname_str(sock));

    *sockp = sock;

error:
    event_destroy(event);

    if (err)
        close(sock);

    return err;
}

int tcp_client (struct event_main *event_main, struct tcp **tcpp, const char *host, const char *port)
{
	int sock;
	int err;
    struct tcp_client client = {
        .event_main     = event_main,
    };
	struct addrinfo hints = {
		.ai_flags		= 0,
		.ai_family		= AF_UNSPEC,
		.ai_socktype	= SOCK_STREAM,
		.ai_protocol	= 0,
	};
	struct addrinfo *addrs, *addr;

	if ((err = getaddrinfo(host, port, &hints, &addrs))) {
		log_perror("getaddrinfo %s:%s: %s", host, port, gai_strerror(err));
		return -1;
	}

	for (addr = addrs; addr; addr = addr->ai_next) {
        if ((err = tcp_client_connect(&client, &sock, addr))) {
            log_perror("%s:%s", addr->ai_canonname, port);
            continue;
        }

        break;
	}

	freeaddrinfo(addrs);

    if (sock < 0)
        return -1;

    log_debug("%d", sock);

	return tcp_create(event_main, tcpp, sock);
}
