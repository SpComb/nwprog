#include "common/tcp.h"
#include "common/tcp_internal.h"

#include "common/log.h"
#include "common/sock.h"
#include "common/stream.h"

#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

struct tcp_server {
    int sock;

    struct event *event;
    
    /* Used for tcp connections */
    struct event_main *event_main;
};

int tcp_listen (int *sockp, const char *host, const char *port, int backlog)
{
    int err;
    struct addrinfo hints = {
        .ai_flags        = AI_PASSIVE,
        .ai_family        = AF_UNSPEC,
        .ai_socktype    = SOCK_STREAM,
        .ai_protocol    = 0,
    };
    struct addrinfo *addrs, *addr;
    int sock = -1;
    
    // translate empty string to NULL
    if (!host || !*host)
        host = NULL;

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
        
        // bind to listen address/port
        if ((err = bind(sock, addr->ai_addr, addr->ai_addrlen)) < 0) {
            log_pwarning("bind");
            close(sock);
            sock = -1;
            continue;
        }

        log_info("%s", sockname_str(sock));

        break;
    }

    freeaddrinfo(addrs);

    if (sock < 0)
        return -1;
    
    // mark as listening
    if ((err = listen(sock, backlog))) {
        log_perror("listen");
        close(sock);
        sock = -1;
    }

    if (sock < 0)
        return -1;
    
    *sockp = sock;

    return 0;
}

int tcp_server (struct event_main *event_main, struct tcp_server **serverp, const char *host, const char *port)
{
    struct tcp_server *server;
    int err;

    if (!(server = calloc(1, sizeof(*server)))) {
        log_perror("calloc");
        return -1;
    }

    server->event_main = event_main;
    
    if ((err = tcp_listen(&server->sock, host, port, TCP_LISTEN_BACKLOG))) {
        log_perror("tcp_listen %s:%s", host, port);
        goto error;
    }

    if ((err = sock_nonblocking(server->sock))) {
        log_error("sock_nonblocking");
        goto error;
    }

    if ((err = event_create(event_main, &server->event, server->sock))) {
        log_error("event_create");
        goto error;
    }

    // ok
    *serverp = server;
    return 0;

error:
    free(server);
    return err;
}

int tcp_server_accept (struct tcp_server *server, struct tcp **tcpp)
{
    int err;
    int sock;

    while ((err = sock_accept(server->sock, &sock)) != 0) {
        // handle various error cases
        if (err < 0 && (errno == EMFILE || errno == ENFILE)) {
            log_pwarning("temporary accept failure: retrying");

            // TODO: some kind of backoff or failure limit, in case all tasks are somehow stuck
            if ((err = event_sleep(server->event, NULL))) {
                log_error("event_sleep");
                return err;
            }

            continue;

        } else if (err < 0) {
            log_error("sock_accept");
            return -1;

        } else {
            // schedule
            if ((err = event_yield(server->event, EVENT_READ, NULL))) {
                log_error("event_yield");
                return err;
            }
        }
    }

    log_info("%s accept %s", sockname_str(sock), sockpeer_str(sock));

    if (tcp_create(server->event_main, tcpp, sock)) {
        log_error("tcp_create");
        return -1;
    }

    return 0;
}

void tcp_server_destroy (struct tcp_server *server)
{
    if (server->event)
        event_destroy(server->event);

    if (server->sock >= 0)
        close(server->sock);

    free(server);
}
