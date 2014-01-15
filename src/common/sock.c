#include "common/sock.h"

#include "common/log.h"

#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>

int sockaddr_buf (char *buf, size_t buflen, const struct sockaddr *sa, socklen_t salen)
{
	char host[NI_MAXHOST];
	char serv[NI_MAXSERV];
	int err;
	
	if ((err = getnameinfo(sa, salen, host, sizeof(host), serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV))) {
		log_warning("getnameinfo: %s", gai_strerror(err));
		return -1;
	}

	snprintf(buf, buflen, "%s:%s", host, serv);

	return 0;
}

const char *sockaddr_str (const struct sockaddr *sa, socklen_t salen)
{
	static char buf[SOCKADDR_MAX];

	if (sockaddr_buf(buf, sizeof(buf), sa, salen)) {
		return NULL;
	}

	return buf;
}

const char * sockname_str (int sock)
{
	static char buf[SOCKADDR_MAX];

	struct sockaddr_storage addr;
	socklen_t addrlen = sizeof(addr);

	if (getsockname(sock, (struct sockaddr *) &addr, &addrlen)) {
		log_pwarning("getsockname(%d)", sock);
		return NULL;
	}

	if (sockaddr_buf(buf, sizeof(buf), (struct sockaddr *) &addr, addrlen)) {
		return NULL;
	}

	return buf;
}

const char * sockpeer_str (int sock)
{
	static char buf[SOCKADDR_MAX];

	struct sockaddr_storage addr;
	socklen_t addrlen = sizeof(addr);

	if (getpeername(sock, (struct sockaddr *) &addr, &addrlen)) {
		log_pwarning("getpeername(%d)", sock);
		return NULL;
	}

	if (sockaddr_buf(buf, sizeof(buf), (struct sockaddr *) &addr, addrlen)) {
		return NULL;
	}

	return buf;

}
