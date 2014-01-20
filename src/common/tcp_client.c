#include "common/tcp.h"

#include "common/log.h"

int tcp_client (struct tcp_stream **streamp, const char *host, const char *port)
{
	int sock;

	if (tcp_connect(&sock, host, port)) {
		log_perror("tcp_connect %s:%s", host, port);
		return -1;
	}
	
	// XXX
	return tcp_stream_create(0, streamp, sock);
}
