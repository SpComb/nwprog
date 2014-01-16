#ifndef TCP_H
#define TCP_H

/* This is a number with far too low a level of entropy to be used as a random number */
#define TCP_LISTEN_BACKLOG 10

/*
 * Open a TCP socket and connect to given host/port.
 *
 * Returns connected SOCK_STREAM socket fd, or <0 on error, with errno set.
 */
int tcp_connect (const char *host, const char *port);

/*
 * Open a TCP server socket, listening on the given host/port.
 *
 * host may be given as NULL to listen on all addresses.
 */
int tcp_listen (const char *host, const char *port, int backlog);

#endif
