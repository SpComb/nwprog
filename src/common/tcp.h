#ifndef TCP_H
#define TCP_H

#include "common/event.h"

/* This is a number with far too low a level of entropy to be used as a random number */
#define TCP_LISTEN_BACKLOG 10

// XXX: bad
#define TCP_STREAM_SIZE 1024

struct tcp;
struct tcp_server;
struct tcp_client;

typedef void (tcp_server_handler)(struct tcp_server *server, struct tcp *tcp, void *ctx);

/*
 * Open a TCP server socket, listening on the given host/port.
 *
 * host may be given as NULL to listen on all addresses.
 */
int tcp_listen (int *sockp, const char *host, const char *port, int backlog);

/*
 * Run a server for accepting connections..
 */
int tcp_server (struct event_main *event_main, struct tcp_server **serverp, const char *host, const char *port);

/*
 * Accept a new incoming request.
 *
 * This will event_yield on the server socket..
 * 
 * TODO: Return >0 on temporary per-client errors?
 */
int tcp_server_accept (struct tcp_server *server, struct tcp **tcpp);

/*
 * Release all resources.
 */ 
void tcp_server_destroy (struct tcp_server *server);

/*
 * Connect to a server..
 */
int tcp_client (struct event_main *event_main, struct tcp **tcpp, const char *host, const char *port);

/*
 * TCP connection interface.
 */ 
int tcp_sock (struct tcp *tcp);
struct stream * tcp_read_stream (struct tcp *tcp);
struct stream * tcp_write_stream (struct tcp *tcp);

/*
 * Set idle timeout for read operations.
 */
void tcp_read_timeout (struct tcp *tcp, const struct timeval *timeout);
void tcp_write_timeout (struct tcp *tcp, const struct timeval *timeout);

void tcp_destroy (struct tcp *tcp);

#endif
