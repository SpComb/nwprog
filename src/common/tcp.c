#include "common/tcp.h"

#include "common/log.h"
#include "common/sock.h"

#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

int tcp_connect (const char *host, const char *port)
{
	int err;
	struct addrinfo hints = {
		.ai_flags		= 0,
		.ai_family		= AF_UNSPEC,
		.ai_socktype	= SOCK_STREAM,
		.ai_protocol	= 0,
	};
	struct addrinfo *addrs, *addr;
	int sock = -1;

	if ((err = getaddrinfo(host, port, &hints, &addrs))) {
		log_perror("getaddrinfo %s:%s: %s", host, port, gai_strerror(err));
		return -1;
	}

	for (addr = addrs; addr; addr = addr->ai_next) {
		if ((sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol)) < 0) {
			log_pwarning("socket(%d, %d, %d)", addr->ai_family, addr->ai_socktype, addr->ai_protocol);
			continue;
		}

		log_info("%s...", sockaddr_str(addr->ai_addr, addr->ai_addrlen));

		if ((err = connect(sock, addr->ai_addr, addr->ai_addrlen)) < 0) {
			log_pwarning("connect");
			close(sock);
			sock = -1;
			continue;
		}

		log_info("%s <- %s", sockpeer_str(sock), sockname_str(sock));

		break;
	}

	freeaddrinfo(addrs);

	// either -1 or valid
	return sock;
}

int tcp_listen (const char *host, const char *port, int backlog)
{
	int err;
	struct addrinfo hints = {
		.ai_flags		= AI_PASSIVE,
		.ai_family		= AF_UNSPEC,
		.ai_socktype	= SOCK_STREAM,
		.ai_protocol	= 0,
	};
	struct addrinfo *addrs, *addr;
	int sock = -1;

	if ((err = getaddrinfo(host, port, &hints, &addrs))) {
		log_perror("getaddrinfo %s:%s: %s", host, port, gai_strerror(err));
		return -1;
	}

	for (addr = addrs; addr; addr = addr->ai_next) {
		if ((sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol)) < 0) {
			log_pwarning("socket(%d, %d, %d)", addr->ai_family, addr->ai_socktype, addr->ai_protocol);
			continue;
		}

		log_info("%s...", sockaddr_str(addr->ai_addr, addr->ai_addrlen));
		
		// bind to listen address/port
		if ((err = bind(sock, addr->ai_addr, addr->ai_addrlen)) < 0) {
			log_pwarning("bind");
			close(sock);
			sock = -1;
			continue;
		}

		log_info("%s", sockname_str(sock));

		break;
	}

	freeaddrinfo(addrs);

	if (sock < 0)
		return sock;
	
	// mark as listening
	if ((err = listen(sock, backlog))) {
		log_perror("listen");
		close(sock);
		sock = -1;
	}

	// either -1 or valid
	return sock;
}
