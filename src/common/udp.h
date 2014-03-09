#ifndef UDP_H
#define UDP_H

#include "common/event.h"

#include <stddef.h>

struct udp;

/**
 * Create a new UDP socket, connected to the given remote host:port.
 */
int udp_connect (struct event_main *event_main, struct udp **udpp, const char *host, const char *port);

/*
 * Receive a UDP datagram on a connected socket.
 *
 * Returns 0 on sucess, 1 on timeout, <0 on error.
 */
int udp_read (struct udp *udp, void *buf, size_t *sizep, const struct timeval *timeout);

/*
 * Send a UDP datagram to a connected endpoint.
 */
int udp_write (struct udp *udp, void *buf, size_t size);

/*
 * The internal event used by the UDP socket.
 *
 * May be NULL if created without event_main.
 */
struct event * udp_event (struct udp *udp);

/*
 * Release all assocated resources.
 */
void udp_destroy (struct udp *udp);

#endif
