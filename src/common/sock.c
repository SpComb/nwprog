#include "common/sock.h"

#include "common/log.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <unistd.h>

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

int sock_nonblocking (int sock)
{
	int flags;

	if ((flags = fcntl(sock, F_GETFL, 0)) < 0) {
		log_perror("fcntl");
		return -1;
	}

	if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
		log_perror("fcntl");
		return -1;
	}

	return 0;
}

int sock_accept (int ssock, int *sockp)
{
    int sock;

    sock = accept(ssock, NULL, NULL);
    
    if (sock >= 0) {
        *sockp = sock;
        return 0;
    
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return 1;

    } else {
        log_perror("accept");
        return -1;
    }
}

int sock_read (int sock, char *buf, size_t *sizep)
{
    int ret = read(sock, buf, *sizep);

    if (ret >= 0) {
        *sizep = ret;
        return 0;

    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return 1;

    } else {
        log_perror("read");
        return -1;
    }
}

int sock_write (int sock, const char *buf, size_t *sizep)
{
    int ret = write(sock, buf, *sizep);

    if (ret >= 0) {
        *sizep = ret;
        return 0;

    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return 1;

    } else {
        log_perror("write");
        return -1;
    }
}

int sock_sendfile (int sock, int fd, size_t *sizep)
{
    int ret = sendfile(sock, fd, NULL, *sizep);

    if (ret > 0) {
        *sizep = ret;
        return 0;

    } else if (!ret) {
        log_debug("eof");
        *sizep = 0;
        return 0;

    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return 1;

    } else {
        log_perror("sendfile");
        return -1;
    }
}
