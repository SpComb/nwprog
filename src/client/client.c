#include "client/client.h"

#include "common/log.h"
#include "common/http.h"
#include "common/tcp.h"
#include "common/sock.h"

#include <stdbool.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/queue.h>

struct client {
    struct tcp *tcp;
	struct http *http;

	/* Settings */
	FILE *response_file;

    /* Headers */
    TAILQ_HEAD(client_headers, client_header) headers;
};

struct client_header {
    const char *name;
    const char *value;

    TAILQ_ENTRY(client_header) client_headers;
};

/*
 * Contents of request sent by client..
 */
struct client_request {
	const char *method;
	const struct url *url;

	/* Headers */
	size_t content_length;

	/* Write request body from FILE */
	FILE *content_file;
};

/*
 * Contents/handling for response from server..
 */
struct client_response {
	unsigned status;

	/* Headers */
	size_t content_length;

	/* Write response content to FILE */
	FILE *content_file;
};

int client_create (struct client **clientp)
{
	struct client *client;

	if (!(client = calloc(1, sizeof(*client)))) {
		log_perror("calloc");
		return -1;
	}

    TAILQ_INIT(&client->headers);
	
	*clientp = client;

	return 0;
}

int client_set_response_file (struct client *client, FILE *file)
{
	if (client->response_file && fclose(client->response_file))
		log_pwarning("fclose");

	client->response_file = file;

	return 0;
}

int client_add_header (struct client *client, const char *name, const char *value)
{
    struct client_header *header;

    if (!(header = calloc(1, sizeof(*header)))) {
        log_perror("calloc");
        return -1;
    }

    header->name = name;
    header->value = value;

    TAILQ_INSERT_TAIL(&client->headers, header, client_headers);

    return 0;
}

int client_open (struct client *client, const struct url *url)
{
	int err;
	const char *port = "http";

	// connect
	if (url->port)
		port = url->port;
	
    if ((err = tcp_client(&client->tcp, url->host, port))) {
        log_error("tcp_client");
        return err;
    }

	// http
	if ((err = http_create(&client->http, tcp_read_stream(client->tcp), tcp_write_stream(client->tcp))))
		return err;

	return 0;
}

// XXX: this function exists only for the logging prefix atm
int client_request_headers (struct client *client, const struct client_request *request)
{
    struct client_header *header;
	int err = 0;

	log_info("\t%20s: %s", "Host", request->url->host);
	err |= http_write_header(client->http, "Host", "%s", request->url->host);

	if (request->content_length) {
		log_info("\t%20s: %zu", "Content-Length", request->content_length);
		err |= http_write_header(client->http, "Content-length", "%zu", request->content_length);
	}

    TAILQ_FOREACH(header, &client->headers, client_headers) {
        log_info("\t%20s: %s", header->name, header->value);
        err |= http_write_header(client->http, header->name, "%s", header->value);
    }

	return err;
}

int client_response_header (struct client *client, struct client_response *response, const char *header, const char *value)
{
	log_info("\t%20s: %s", header, value);

	if (strcasecmp(header, "Content-Length") == 0) {
		if (sscanf(value, "%zu", &response->content_length) != 1) {
			log_warning("invalid content_length: %s", value);
			return 1;
		}

		log_debug("content_length=%zu", response->content_length);
	}

	return 0;
}

static int client_request (struct client *client, struct client_request *request, struct client_response *response)
{
	int err;

	// request
	log_info("%s http://%s/%s", request->method, tcp_peer_str(client->tcp), request->url->path);

	if ((err = http_write_request(client->http, request->method, "/%s", request->url->path))) {
		log_error("error sending request line");
		return err;
	}

	if ((err = client_request_headers(client, request))) {
		log_error("error sending request headers");
		return err;
	}

	if ((err = http_write_headers(client->http))) {
		log_error("error sending request end-of-headers");
		return err;
	}

	if (request->content_file && (err = http_write_file(client->http, request->content_file, request->content_length)))
		return err;

	log_debug("end-of-request");
	
	// response
	{
		const char *reason, *version;

		if ((err = http_read_response(client->http, &version, &response->status, &reason))) {
			log_error("error reading response line");
			return err;
		}
		
		log_info("%u %s", response->status, reason);
	}

	// response headers
	{
		const char *header, *value;
		
		// *header is preserved for folded header lines... so they appear as duplicate headers
		while (!(err = http_read_header(client->http, &header, &value))) {
			if ((err = client_response_header(client, response, header, value)))
				return err;
		}

		if (err < 0) {
			log_error("error reading response headers");
			return err;
		}
	}

	// response body
	if (
			(response->status >= 100 && response->status <= 199)
		||	(response->status == 204 || response->status == 304)
		||	strcasecmp(request->method, "HEAD") == 0
	) {
		log_debug("no body for 1xx 204 304 or HEAD response");

		// more requests
		err = 0;

	} else if (response->content_length) {
		if ((err = http_read_file(client->http, response->content_file, response->content_length)))
			return err;
		
		// more requests
		err = 0;

	} else {
		if ((err = http_read_file(client->http, response->content_file, 0)))
			return err;

		// to end of connection
		err = 1;
	}
	
	log_info("");

	return err;
}

int client_get (struct client *client, const struct url *url)
{
	int err;

	struct client_request request = {
		.url	= url,
		.method	= "GET",
	};

	struct client_response response = {
		.content_file	= client->response_file,
	};
	
	if ((err = client_request(client, &request, &response)) < 0)
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
	struct client_request request = {
		.url			= url,
		.method			= "PUT",

		.content_length	= content_length,
		.content_file	= file,
	};

	struct client_response response	= {
		.content_file	= client->response_file,
	};

	if ((err = client_request(client, &request, &response)) < 0)
		return err;

	return 0;
}

void client_destroy (struct client *client)
{
    struct client_header *header;

    while ((header = TAILQ_FIRST(&client->headers))) {
        TAILQ_REMOVE(&client->headers, header, client_headers);
        free(header);
    }

    if (client->http)
        http_destroy(client->http);

	free(client);
}
