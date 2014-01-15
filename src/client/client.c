#include "client/client.h"

#include "common/log.h"
#include "common/http.h"
#include "common/tcp.h"
#include "common/sock.h"

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

int client_request_headers (struct client *client, const struct url *url)
{
	log_info("\t%20s: %s", "Host", url->path);
	return http_client_request_header(client->http, "Host", url->host);
}

int client_response_header (struct client *client, const char *header, const char *value)
{
	log_info("\t%20s: %s", header, value);

	return 0;
}

int client_get (struct client *client, const struct url *url)
{
	int err;

	// request
	log_info("GET http://%s/%s", sockpeer_str(client->sock), url->path);

	if ((err = http_client_request_start_path(client->http, "GET", "/%s", url->path))) {
		log_error("error sending request line");
		return err;
	}

	if ((err = client_request_headers(client, url))) {
		log_error("error sending request headers");
		return err;
	}

	if ((err = http_client_request_end(client->http))) {
		log_error("error sending request end-of-headers");
		return err;
	}

	// response	
	const char *version, *reason;
	unsigned status;

	if ((err = http_client_response_start(client->http, &version, &status, &reason))) {
		log_error("error reading response line");
		return err;
	}
	
	log_info("GET http://%s/%s -> %u %s", sockpeer_str(client->sock), url->path, status, reason);

	const char *header, *value;
	
	// *header is preserved for folded header lines... so they appear as duplicate headers
	while (!(err = http_client_response_header(client->http, &header, &value))) {
		if ((err = client_response_header(client, header, value)))
			return err;
	}

	if (err < 0) {
		log_error("error reading response headers");
		return err;
	}

	// body
	char buf[512];
	size_t len = sizeof(buf);
	int ret;

	while (!(err = http_client_response_body(client->http, buf, &len))) {
		// copy to stdout
		if ((ret = fwrite(buf, len, 1, stdout)) != 1) {
			log_pwarning("fwrite");
			return -1;
		}
	}

	if (err < 0) {
		log_error("error reading response body");
		return err;
	}

	return 0;
}

void client_destroy (struct client *client)
{
	free(client);
}
