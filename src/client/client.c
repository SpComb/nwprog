#include "client/client.h"

#include "common/log.h"
#include "common/http.h"
#include "common/tcp.h"
#include "common/sock.h"

#include <stdbool.h>
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

int client_request_headers (struct client *client, const struct url *url, size_t content_length)
{
	int err = 0;

	log_info("\t%20s: %s", "Host", url->path);
	err |= http_client_request_header(client->http, "Host", url->host);

	if (content_length) {
		log_info("\t%20s: %zu", "Content-Length", content_length);
		err |= http_client_request_headerf(client->http, "Content-length", "%zu", content_length);
	}

	return err;
}

int client_request_file (struct client *client, size_t content_length, FILE *file)
{
	char buf[512], *bufp;
	size_t ret, len = sizeof(buf);

	do {
		if ((ret = fread(buf, 1, sizeof(buf), file)) < 0) {
			log_pwarning("fread");
			return -1;
		}

		log_debug("fread: %zu", ret);

		if (!ret)
			// EOF
			return 0;

		bufp = buf;
		len = ret;
		
		while (ret) {
			if (http_client_request_body(client->http, bufp, &len)) {
				log_error("error writing request body");
				return -1;
			}

			log_debug("http_client_request_body: %zu", len);

			bufp += len;
			ret -= len;
		}
	} while (true);
}

int client_response_header (struct client *client, const char *header, const char *value)
{
	log_info("\t%20s: %s", header, value);

	return 0;
}

static int client_request (struct client *client, const struct url *url, const char *method, size_t content_length, FILE *file)
{
	int err;

	// request
	log_info("%s http://%s/%s", method, sockpeer_str(client->sock), url->path);

	if ((err = http_client_request_start_path(client->http, method, "/%s", url->path))) {
		log_error("error sending request line");
		return err;
	}

	if ((err = client_request_headers(client, url, content_length))) {
		log_error("error sending request headers");
		return err;
	}

	if ((err = http_client_request_end(client->http))) {
		log_error("error sending request end-of-headers");
		return err;
	}

	if (file && (err = client_request_file(client, content_length, file)))
		return err;

	log_debug("end-of-request");

	return 0;
}

static int client_response (struct client *client)
{
	const char *version, *reason;
	unsigned status;
	int err;

	if ((err = http_client_response_start(client->http, &version, &status, &reason))) {
		log_error("error reading response line");
		return err;
	}
	
	log_info("%u %s", status, reason);

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
	
	log_debug("end-of-response");

	return 0;
}

int client_get (struct client *client, const struct url *url)
{
	int err;
	
	// request
	if ((err = client_request(client, url, "GET", 0, NULL)))
		return err;

	// response	
	if ((err = client_response(client)))
		return err;

	return 0;
}

int client_put (struct client *client, const struct url *url, FILE *file)
{
	int err;

	// determine the file size
	int content_length;

	if (fseek(file, 0, SEEK_END)) {
		log_perror("given PUT file is not seekable");
		return 1;
	}

	if ((content_length = ftell(file)) < 0) {
		log_perror("ftell");
		return -1;
	}

	if (fseek(file, 0, SEEK_SET)) {
		log_perror("fseek");
		return -1;
	}
	
	// request
	if ((err = client_request(client, url, "PUT", content_length, file)))
		return err;

	// response	
	if ((err = client_response(client)))
		return err;

	return 0;
}

void client_destroy (struct client *client)
{
	free(client);
}
