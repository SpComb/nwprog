#ifndef TCP_INTERNAL_H
#define TCP_INTERNAL_H

#include "common/event.h"
#include "common/stream.h"

struct tcp {
	int sock;
	
	struct event *event;
    
	struct timeval read_timeout, write_timeout;

	struct stream *read, *write;
};

/*
 * Initialize a new TCP connection for use with its read/write streams.
 *
 * event_main is optional, and if given, used for event/task based nonblocking reads/writes.
 */
int tcp_create (struct event_main *event_main, struct tcp **tcpp, int sock);

#endif
