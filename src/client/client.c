#include "client/client.h"

#include "common/log.h"
#include "common/http.h"
#include "common/tcp.h"
#include "common/sock.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/queue.h>

struct client {
	/* Settings */
#ifdef WITH_SSL
    struct ssl_main *ssl_main;
#endif
	FILE *response_file;

    /* Transport; either-or */
    struct tcp *tcp;
#ifdef WITH_SSL
    struct ssl *ssl;
#endif

    /* Protocol; NULL if not open. */
	struct http *http;

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
    bool content_length_zero;
	size_t content_length;

	/* Write response content to FILE */
	FILE *content_file;

    /* Close connection after response */
    bool close;
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
#ifdef WITH_SSL
int client_set_ssl (struct client *client, struct ssl_main *ssl_main)
{
    client->ssl_main = ssl_main;

    return 0;
}
#endif
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

int client_open_http (struct client *client, const struct url *url)
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

#ifdef WITH_SSL
int client_open_https (struct client *client, const struct url *url)
{
	int err;
	const char *port = "https";

    if (!client->ssl_main) {
        log_error("no client ssl support; use client_set_ssl()");
        return 1;
    }

	// connect
	if (url->port)
		port = url->port;
	
    if ((err = ssl_client(client->ssl_main, &client->ssl, url->host, port))) {
        log_error("ssl_client");
        return err;
    }

	// http
	if ((err = http_create(&client->http, ssl_read_stream(client->ssl), ssl_write_stream(client->ssl))))
		return err;

	return 0;
}
#endif

int client_open (struct client *client, const struct url *url)
{
    if (!url->scheme || !*url->scheme || strcmp(url->scheme, "http") == 0) {
        return client_open_http(client, url);
        
    } else if (strcmp(url->scheme, "https") == 0) {
#ifdef WITH_SSL
        return client_open_https(client, url);
#else
        log_error("unsupported url scheme: %s", url->scheme);
        return 1;
#endif
    } else {
        log_error("unknown url scheme: %s", url->scheme);
        return 1;
    }
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

/*
 * Send request body from file.
 */
int client_request_file (struct client *client, const struct client_request *request)
{
    int fd;
    int err;

	if (!request->content_file)
        return 0;

    // XXX: convert 
    if (fflush(request->content_file)) {
        log_perror("fflush");
        return -1;
    }

    if ((fd = fileno(request->content_file)) < 0) {
        log_perror("fileno");
        return -1;
    }
    
    // send; content_length may either be 0 or determined earlier, before sending headers
    if ((err = http_write_file(client->http, fd, request->content_length)))
		return err;
    
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

        if (!response->content_length)
            response->content_length_zero = true;

	} else if (strcasecmp(header, "Connection") == 0) {
        if (strcasecmp(value, "close") == 0) {
            log_debug("explicit connection-close");

            response->close = true;
        } else {
            log_debug("unknown Connection: %s", value);
        }
    }

	return 0;
}

/*
 * Read response body to file.
 */
int client_response_file (struct client *client, struct client_response *response)
{
    int fd;
    int err;

	if (!response->content_file)
        return 0;

    // XXX: convert 
    if (fflush(response->content_file)) {
        log_perror("fflush");
        return -1;
    }

    if ((fd = fileno(response->content_file)) < 0) {
        log_perror("fileno");
        return -1;
    }
    
    // send; content_length may either be 0 or determined earlier, when reading headers
    if ((err = http_read_file(client->http, fd, response->content_length)))
		return err;
    
    return 0;
}

/*
 * Send one request, read and process the response.
 *
 * Returns 0 on success with a persistent connection, 1 on success with a non-persistent connection, <0 on error.
 */
static int client_request (struct client *client, struct client_request *request, struct client_response *response)
{
	int err;
    
    // request
    {
        // url parses these parts separately, so we must handle them separately..
        if (request->url->query && *request->url->query) {
            log_info("%s /%s?%s", request->method, request->url->path, request->url->query);

            err = http_write_request(client->http, request->method, "/%s?%s", request->url->path, request->url->query);

        } else {
            log_info("%s /%s", request->method, request->url->path);

            err = http_write_request(client->http, request->method, "/%s", request->url->path);
        }

        if (err) {
            log_error("error sending request line");
            return -1;
        }

        if ((err = client_request_headers(client, request))) {
            log_error("error sending request headers");
            return -1;
        }

        if ((err = http_write_headers(client->http))) {
            log_error("error sending request end-of-headers");
            return -1;
        }

        if ((err = client_request_file(client, request))) {
            log_error("error sending request file");
            return -1;
        }
    }

	log_debug("end-of-request");
	
	// response
	{
		const char *reason, *version;

		if ((err = http_read_response(client->http, &version, &response->status, &reason))) {
			log_error("error reading response line");
			return -1;
		}
		
		log_info("%s %u %s", version, response->status, reason);
	}

	// response headers
	{
		const char *header, *value;
		
		// *header is preserved for folded header lines... so they appear as duplicate headers
		while (!(err = http_read_header(client->http, &header, &value))) {
			if ((err = client_response_header(client, response, header, value)))
				return -1;
		}

		if (err < 0) {
			log_error("error reading response headers");
			return -1;
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
        if ((err = client_response_file(client, response)))
            return err;
		
		// more requests
		err = 0;

	} else if (response->content_length_zero) {
        log_debug("explicit zero-length response body");

        // more requests
        err = 0;

    } else {
		// to EOF
        if ((err = client_response_file(client, response)))
            return err;

        // no more requests
		err = 1;
	}

    if (response->close) {
        log_debug("explicit close-response");

        err = 1;
    }
	
	log_info("%s", "");

	return err;
}

int client_get (struct client *client, const struct url *url)
{
	int err;

    // open connection if needed
    if (!client->http && (err = client_open(client, url)))
        return err;

	struct client_request request = {
		.url	= url,
		.method	= "GET",
	};

	struct client_response response = {
		.content_file	= client->response_file,
	};
	
	if ((err = client_request(client, &request, &response)) < 0) {
        log_error("client_request");
    }

    // close if not persistent, or error
    if (err) {
       if ((err = client_close(client)))
            log_warning("client_close");
    }

    return response.status;
}

int client_put (struct client *client, const struct url *url, FILE *file)
{
	int err;

	// determine the file size
	int content_length;

	if (fseek(file, 0, SEEK_END)) {
		log_perror("given PUT file is not seekable");
		return -1;
	}

	if ((content_length = ftell(file)) < 0) {
		log_perror("ftell");
		return -1;
	}

	if (fseek(file, 0, SEEK_SET)) {
		log_perror("fseek");
		return -1;
	}
	
    // open connection if needed
    if (!client->http && (err = client_open(client, url)))
        return err;

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

	if ((err = client_request(client, &request, &response)) < 0) {
        log_error("client_request");
    }

    // close if not persistent, or error
    if (err) {
       if ((err = client_close(client)))
            log_warning("client_close");
    }

    return response.status;
}

int client_close (struct client *client)
{
    log_info("%s", "");

    http_destroy(client->http); client->http = NULL;

#ifdef WITH_SSL
    if (client->ssl) {
        // XXX: clean close
        ssl_destroy(client->ssl);
        client->ssl = NULL;

    } else if (client->tcp) {
#else
    if (client->tcp) {
#endif
        // XXX: clean FIN, vs RST?
        tcp_destroy(client->tcp);
        client->tcp = NULL;;
    }

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
