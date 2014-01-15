#include "client/client.h"

#include "common/log.h"
#include "common/tcp.h"

#include <stdlib.h>

struct client {
	int socket;
};

/*
 * Attempt to connect to the given server.
 */
int client_connect (struct client *client, const char *host, const char *port)
{
	if ((client->socket = tcp_connect(host, port)) < 0) {
		log_perror("%s:%s", host, port);
		return -1;
	}

	return 0;
}

int client_create (struct client **clientp)
{
	struct client *client;

	if (!(client = calloc(1, sizeof(*client)))) {
		log_perror("calloc");
		return -1;
	}
	
	*clientp = client;

	return 0;
}

int client_open (struct client *client, const struct url *url)
{
	int err;
	const char *port = "http";

	if (url->port)
		port = url->port;

	if ((err = client_connect(client, url->host, port)))
		return err;
}

void client_destroy (struct client *client)
{
	free(client);
}
