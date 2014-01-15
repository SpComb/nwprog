#include "client/client.h"

#include "common/log.h"
#include "common/http.h"
#include "common/tcp.h"

#include <stdlib.h>

struct client {
	int sock;

	struct http *http;
};

/*
 * Attempt to connect to the given server.
 */
int client_connect (struct client *client, const char *host, const char *port)
{
	if ((client->sock = tcp_connect(host, port)) < 0) {
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

	// connect
	if (url->port)
		port = url->port;
	
	if ((err = client_connect(client, url->host, port)))
		return err;

	// http
	if ((err = http_create(&client->http, client->sock)))
		return err;

	return 0;
}

int client_get (struct client *client, const struct url *url)
{
	int err;

	if ((err = http_request_start_path(client->http, "GET", "/%s", url->path)))
		return err;

	if ((err = http_request_header(client->http, "Host", url->host)))
		return err;

	if ((err = http_request_end(client->http)))
		return err;

	return 0;
}

void client_destroy (struct client *client)
{
	free(client);
}
