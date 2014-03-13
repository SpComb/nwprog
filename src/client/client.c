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
    struct event_main *event_main;

#ifdef WITH_SSL
    struct ssl_main *ssl_main;
#endif

    /* Copy out response body */
	FILE *response_file;

    /* Close response_file after use */
    bool response_file_close;

    /* Send HTTP/1.1 requests */
    bool request_http11;

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

    /* Content-length if give as non-zero, or zero */
	size_t content_length;

    /* Content-length given as zero */
    bool content_length_zero;

    /* Transfer-encoding: chunked */
    bool chunked;

	/* Write response content to FILE */
	FILE *content_file;

    /* Close connection after response */
    bool close;
};

int client_create (struct event_main *event_main, struct client **clientp)
{
	struct client *client;

	if (!(client = calloc(1, sizeof(*client)))) {
		log_perror("calloc");
		return -1;
	}

    TAILQ_INIT(&client->headers);
    
    client->event_main = event_main;
	
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

int client_set_response_file (struct client *client, FILE *file, bool close)
{
	if (client->response_file && client->response_file_close) {
        log_debug("closing: %p", client->response_file);

        if (fclose(client->response_file))
    		log_pwarning("fclose");
    }

	client->response_file = file;
    client->response_file_close = close;

	return 0;
}

int client_set_request_version (struct client *client, enum http_version version)
{
    switch (version) {
        case HTTP_10:
            client->request_http11 = false;
            return 0;

        case HTTP_11:
            client->request_http11 = true;
            return 0;

        default:
            log_error("unknown http_version: %d", version);
            return -1;
    }
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
	
    if ((err = tcp_client(client->event_main, &client->tcp, url->host, port))) {
        log_error("tcp_client");
        return err;
    }

    log_info("%s", sockpeer_str(tcp_sock(client->tcp)));

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
    
    log_info("%s", sockpeer_str(ssl_sock(client->ssl)));

	// http
	if ((err = http_create(&client->http, ssl_read_stream(client->ssl), ssl_write_stream(client->ssl))))
		return err;

	return 0;
}
#endif

int client_open (struct client *client, const struct url *url)
{
    int err;

    if (client->http) {
        if ((err = client_close(client))) {
            log_warning("failed to close old connection while opening new one, continuing regardless");
        }
    }

    if (!url->host || !*url->host) {
        log_error("no host given");
        return 1;
    }

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

int client_request_header (struct client *client, const char *name, const char *fmt, ...)
{
    va_list args;
    int err;
	
    va_start(args, fmt);
    log_ninfo("\t%20s: ", name);
    logv_qinfo(fmt, args);
    va_end(args);
            

    va_start(args, fmt);
    err = http_write_headerv(client->http, name, fmt, args);
    va_end(args);

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
    } else if (strcasecmp(header, "Transfer-Encoding") == 0) {
        if (strcasecmp(value, "chunked") == 0) {
            log_debug("chunked response");

            response->chunked = true;

        } else if (strcasecmp(value, "identity") == 0) {
            response->chunked = false;

        } else {
            log_warning("unknown transfer-encoding: %s", value);
        }
    }

	return 0;
}

/*
 * Read response body to file.
 */
int client_response_file (struct client *client, struct client_response *response, size_t content_length)
{
    int fd;
    int err;

	if (response->content_file) {
        // convert FILE* to fd
        if (fflush(response->content_file)) {
            log_perror("fflush");
            return -1;
        }

        if ((fd = fileno(response->content_file)) < 0) {
            log_perror("fileno");
            return -1;
        }

    } else {
        // discard response body
        fd = -1;
    }

    if (response->chunked) {
        // read in chunks
        err = http_read_chunked_file(client->http, fd);

    } else {
        // content_length may either be 0 or determined earlier, when reading headers
        err = http_read_file(client->http, fd, content_length);
    }

    return err;
}

int client_response (struct client *client, struct client_request *request, struct client_response *response)
{
    int err;
    const char *reason, *version;

    if ((err = http_read_response(client->http, &version, &response->status, &reason))) {
        log_error("error reading response line");
        return err;
    }
    
    log_info("%s %u %s", version, response->status, reason);

	// headers
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

	// body
	if (
			(response->status >= 100 && response->status <= 199)
		||	(response->status == 204 || response->status == 304)
		||	strcasecmp(request->method, "HEAD") == 0
	) {
		log_debug("no body for 1xx 204 304 or HEAD response");

		// more requests
		err = 0;

    } else if (response->chunked) {
        log_debug("response-body chunked");

        if ((err = client_response_file(client, response, 0)))
            return err;

        // more requests
        err = 0;

	} else if (response->content_length) {
        log_debug("response-body content-length=%zu", response->content_length);

        if ((err = client_response_file(client, response, response->content_length)))
            return err;
		
		// more requests
		err = 0;

	} else if (response->content_length_zero) {
        log_debug("response-body zero-length");

        // more requests
        err = 0;

    } else {
        log_debug("response-body eof");

        if ((err = client_response_file(client, response, 0)))
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
        const char *version = client->request_http11 ? "HTTP/1.1" : "HTTP/1.0";

        // url parses these parts separately, so we must handle them separately..
        if (request->url->query && *request->url->query) {
            log_info("%s /%s?%s %s", request->method, request->url->path, request->url->query, version);

            err = http_write_request(client->http, version, request->method, "/%s?%s", request->url->path, request->url->query);

        } else {
            log_info("%s /%s %s", request->method, request->url->path, version);

            err = http_write_request(client->http, version, request->method, "/%s", request->url->path);
        }

        if (err) {
            log_error("error sending request line");
            return -1;
        }

        if ((err = client_request_header(client, "Host", "%s", request->url->host))) {
            log_error("error sending request host header");
            return err;
        }

        if (request->content_length) {
            if ((err = client_request_header(client, "Content-length", "%zu", request->content_length))) {
                log_error("error sending request content-length header");
                return err;
            }
        }

        // custom headers
        struct client_header *header;

        TAILQ_FOREACH(header, &client->headers, client_headers) {
            if ((err = client_request_header(client, header->name, "%s", header->value))) {
                log_error("error sending request header: %s", header->name);
                return err;
            }
        }

        if ((err = http_write_headers(client->http))) {
            log_error("error sending request end-of-headers");
            return -1;
        }

        if ((err = client_request_file(client, request))) {
            log_error("error sending request file");
            return -1;
        }

        log_info("%s", "");
    }
	
	// response
    return client_response(client, request, response);
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
       if (client_close(client))
            log_warning("client_close");
    }

    if (err < 0) {
        return err;
    } else {
        return response.status;
    }
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
       if (client_close(client))
            log_warning("client_close");
    }

    if (err < 0) {
        return err;
    } else {
        return response.status;
    }
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

    if (client->response_file && client->response_file_close)
        fclose(client->response_file);

	free(client);
}
