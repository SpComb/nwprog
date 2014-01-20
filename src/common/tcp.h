#ifndef TCP_H
#define TCP_H

#include "common/event.h"

/* This is a number with far too low a level of entropy to be used as a random number */
#define TCP_LISTEN_BACKLOG 10

// XXX: bad
#define TCP_STREAM_SIZE 1024

struct tcp_server;
struct tcp_stream;
struct tcp_client;

typedef void (tcp_server_handler)(struct tcp_server *server, struct tcp_stream *stream, void *ctx);

/*
 * Open a TCP socket and connect to given host/port.
 */
int tcp_connect (int *sockp, const char *host, const char *port);

/*
 * Open a TCP server socket, listening on the given host/port.
 *
 * host may be given as NULL to listen on all addresses.
 */
int tcp_listen (int *sockp, const char *host, const char *port, int backlog);

/*
 * Run a server for accepting connections..
 */
int tcp_server (struct event_main *event_main, struct tcp_server **serverp, const char *host, const char *port, tcp_server_handler *func, void *ctx);

/*
 * Connect to a server..
 *
 * XXX: blocking
 */
int tcp_client (struct tcp_stream **streamp, const char *host, const char *port);

// XXX
int tcp_stream_create (struct event_main *event_main, struct tcp_stream **streamp, int sock);

struct stream * tcp_stream_read (struct tcp_stream *stream);
struct stream * tcp_stream_write (struct tcp_stream *stream);

const char * tcp_stream_sock_str (struct tcp_stream *stream);
const char * tcp_stream_peer_str (struct tcp_stream *stream);
    
void tcp_stream_destroy (struct tcp_stream *stream);

#endif
