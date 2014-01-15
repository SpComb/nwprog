#ifndef TCP_H
#define TCP_H

/*
 * Open a TCP socket and connect to given host/port.
 *
 * Returns connected SOCK_STREAM socket fd, or <0 on error, with errno set.
 */
int tcp_connect (const char *host, const char *port);

#endif
