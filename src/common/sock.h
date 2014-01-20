#ifndef SOCK_H
#define SOCK_H

#include <sys/socket.h>

#define SOCKADDR_MAX 1024

/*
 * Format host:srv for given sockaddr into given buf, should should be at least SOCKADDR_MAX len.
 */
int sockaddr_buf (char *buf, size_t buflen, const struct sockaddr *sa, socklen_t salen);

/*
 * Return a (static) pointer to the host/serv for the given sockaddr.
 */
const char * sockaddr_str (const struct sockaddr *sa, socklen_t salen);

/*
 * Return a (static) pointer to the local host:serv for the given socket.
 */
const char * sockname_str (int sock);

/*
 * Return a (static) pointer to the remote host:serv for the given socket.
 */
const char * sockpeer_str (int sock);

/*
 * Make socket nonblocking
 */
int sock_nonblocking (int sock);

/*
 * Accept a new socket connection on a listen() socket.
 *
 * Returns 1 on nonblocking, 0 on success, <0 on error.
 */
int sock_accept (int ssock, int *sockp);

#endif
