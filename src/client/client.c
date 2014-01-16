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
	int sock;

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
	const struct url *url;
	const char *method;

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
	const char *reason;

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

/*
 * Attempt to connect to the given server.
 */
static int client_connect (struct client *client, const char *host, const char *port)
{
	if ((client->sock = tcp_connect(host, port)) < 0) {
		log_perror("%s:%s", host, port);
		return -1;
	}

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

// XXX: this function exists only for the logging prefix atm
int client_request_headers (struct client *client, const struct client_request *request)
{
    struct client_header *header;
	int err = 0;

	log_info("\t%20s: %s", "Host", request->url->host);
	err |= http_client_request_header(client->http, "Host", request->url->host);

	if (request->content_length) {
		log_info("\t%20s: %zu", "Content-Length", request->content_length);
		err |= http_client_request_headerf(client->http, "Content-length", "%zu", request->content_length);
	}

    TAILQ_FOREACH(header, &client->headers, client_headers) {
        log_info("\t%20s: %s", header->name, header->value);
        err |= http_client_request_header(client->http, header->name, header->value);
    }

	return err;
}

/*
 * Write contents of FILE to request.
 */
int client_request_file (struct client *client, size_t content_length, FILE *file)
{
	char buf[512], *bufp;
	size_t ret, len = sizeof(buf);

	// TODO: respect content_length
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

static int client_request (struct client *client, const struct client_request *request)
{
	int err;

	// request
	log_info("%s http://%s/%s", request->method, sockpeer_str(client->sock), request->url->path);

	if ((err = http_client_request_start_path(client->http, request->method, "/%s", request->url->path))) {
		log_error("error sending request line");
		return err;
	}

	if ((err = client_request_headers(client, request))) {
		log_error("error sending request headers");
		return err;
	}

	if ((err = http_client_request_end(client->http))) {
		log_error("error sending request end-of-headers");
		return err;
	}

	if (request->content_file && (err = client_request_file(client, request->content_length, request->content_file)))
		return err;

	log_debug("end-of-request");

	return 0;
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

/*
 * Write contents of response to FILE, or discard if NULL.
 */
static int client_response_file (struct client *client, FILE *file, size_t content_length)
{
	char buf[512];
	size_t len = sizeof(buf), ret;
	int err;

	if (!content_length && file) {
		log_debug("no content");
	}

	while (content_length) {
		len = sizeof(buf);

		log_debug("content_length: %zu", content_length);
		
		// cap to expected dat
		if (content_length < len)
			len = content_length;

		if ((err = http_client_response_body(client->http, buf, &len)) < 0) {
			log_error("error reading response body");
			return err;
		}
		
		log_debug("read: %zu", len);

		if (err) {
			// EOF
			break;
		}

		// copy to stdout
		if (file && (ret = fwrite(buf, len, 1, file)) != 1) {
			log_pwarning("fwrite");
			return -1;
		}
		
		log_debug("write: %zu", len);
		
		// sanity-check
		if (len <= content_length) {
			content_length -= len;
		} else {
			log_fatal("BUG: len=%zu > content_length=%zu", len, content_length);
			return -1;
		}
	}
	
	if (content_length) {
		log_warning("missing content: %zu", content_length);
	}

	return 0;
}

static int client_response (struct client *client, struct client_response *response)
{
	const char *version;
	int err;

	if ((err = http_client_response_start(client->http, &version, &response->status, &response->reason))) {
		log_error("error reading response line");
		return err;
	}
	
	log_info("%u %s", response->status, response->reason);

	const char *header, *value;
	
	// *header is preserved for folded header lines... so they appear as duplicate headers
	while (!(err = http_client_response_header(client->http, &header, &value))) {
		if ((err = client_response_header(client, response, header, value)))
			return err;
	}

	if (err < 0) {
		log_error("error reading response headers");
		return err;
	}

	// TODO: figure out if we actually have one
	if ((err = client_response_file(client, response->content_file, response->content_length)))
		return err;
	
	log_debug("end-of-response");

	return 0;
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
	
	if ((err = client_request(client, &request)))
		return err;

	if ((err = client_response(client, &response)))
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

	if ((err = client_request(client, &request)))
		return err;

	if ((err = client_response(client, &response)))
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
