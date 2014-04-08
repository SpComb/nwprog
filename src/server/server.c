#include "server/server.h"

#include "common/http.h"
#include "common/log.h"
#include "common/sock.h"
#include "common/tcp.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <unistd.h>

struct server {
    struct event_main *event_main;
    
    /* Listen tasks */
	TAILQ_HEAD(server_listens, server_listen) listens;

    /* Handler lookup */
	TAILQ_HEAD(server_handlers, server_handler_item) handlers;

    /* Response headers */
    TAILQ_HEAD(server_headers, server_header) headers;
};

struct server_listen {
    struct server *server;
    struct tcp_server *tcp;

    TAILQ_ENTRY(server_listen) server_listens;
};

struct server_handler_item {
	const char *method;
	const char *path;

	struct server_handler *handler;

	TAILQ_ENTRY(server_handler_item) server_handlers;
};

struct server_header {
    char *name;
    char *value;

    TAILQ_ENTRY(server_header) server_headers;
};

struct server_client {
    struct server *server;
    struct tcp *tcp;
	struct http *http;

	/* Request */
    struct server_request {
        /* Storage for request method field */
        char method[HTTP_METHOD_MAX];

        /* Storage for request path field; this is decoded into url and contains embedded NULs1 */
        char pathbuf[HTTP_PATH_MAX];

        /* Storage for request Host header */
        char hostbuf[HTTP_HOST_MAX];

        /* Decoded request URL, including query */
        struct url url;

        /* Size of request entity, or zero */
        size_t content_length;

        /* Does the client support HTTP/1.1? */
        bool http11;

        /* Progress */
        bool request;
        bool header;
        bool headers;
        bool body;

    } request;
	
	/* Response */
    struct server_response {
        /* The response status has been sent */
        unsigned status;

        /* One or many response headers have been sent */
        bool header;

        /* Response end-of-headers have been sent */
        bool headers;

        /* Response body has been started */
        bool body;

        /* Response entity body is being sent using chunked transfer encoding; must be ended */
        bool chunked;

        /* Close connection after response; may be determined by client or by response method */
        /* TODO: Some HTTP/1.0 clients may send a Connection:keep-alive request header, whereupon
         * it may be possible to have !request.http11 && !response.close, in which situation we should
         * be returning a 'Connection: keep-alive' response header...
         */ 
        bool close;

    } response;

    int err;
};

/* Idle timeout used for client reads; reset on every read operation */
static const struct timeval SERVER_READ_TIMEOUT = { .tv_sec = 10 };

/* Idle timeout used for client write buffering; reset on every write operation */
static const struct timeval SERVER_WRITE_TIMEOUT = { .tv_sec = 10 };

int server_create (struct event_main *event_main, struct server **serverp)
{
	struct server *server = NULL;

	if (!(server = calloc(1, sizeof(*server)))) {
		log_perror("calloc");
		return -1;
	}

	TAILQ_INIT(&server->listens);
	TAILQ_INIT(&server->handlers);
    TAILQ_INIT(&server->headers);

    server->event_main = event_main;

	*serverp = server;

	return 0;
}

int server_add_handler (struct server *server, const char *method, const char *path, struct server_handler *handler)
{
	struct server_handler_item *h = NULL;

	if (!handler) {
		log_fatal("NULL handler given");
		return -1;
	}

	if (!(h = calloc(1, sizeof(*h)))) {
		log_pwarning("calloc");
		return -1;
	}
	
	h->method = method;
	h->path = path;
	h->handler = handler;

	TAILQ_INSERT_TAIL(&server->handlers, h, server_handlers);

    // export state to handler
    handler->event_main = server->event_main;

	return 0;
}

/*
 * Lookup a handler for the given request.
 */
int server_lookup_handler (struct server *server, const char *method, const char *path, struct server_handler **handlerp)
{
	struct server_handler_item *h;
	enum http_status status = 404;

	TAILQ_FOREACH(h, &server->handlers, server_handlers) {
		if (h->path && strncmp(h->path, path, strlen(h->path)))
			continue;
		
		if (h->method && strcmp(h->method, method)) {
			// chain along so that a matching path but mismatching method is 405
			status = 405;
			continue;
		}
		
		log_debug("%s", h->path);

		*handlerp = h->handler;
		return 0;
	}
	
	log_warning("%s: %d", path, status);
	return status;
}

int server_add_header (struct server *server, const char *name, const char *value)
{
    struct server_header *header;

    if (!(header = calloc(1, sizeof(*header)))) {
        log_perror("calloc");
        return -1;
    }

    if (!(header->name = strdup(name))) {
        log_perror("strdup");
        goto error;
    }

    if (!(header->value = strdup(value))) {
        log_perror("strdup");
        goto error;
    }

    // ok
    TAILQ_INSERT_TAIL(&server->headers, header, server_headers);

    return 0;

error:
    free(header->value);
    free(header->name);
    free(header);

    return -1;
}

/*
 * Read the client request line.
 *
 * Returns 0 on success, <0 on internal error, 1 on EOF, http 4xx on client error.
 */
int server_request (struct server_client *client)
{
	const char *method, *path, *version;
	int err;

    if (client->request.request) {
        log_fatal("re-reading request");
        return -1;
    }

	if ((err = http_read_request(client->http, &method, &path, &version)))
        return err;

	if (strlen(method) >= sizeof(client->request.method)) {
		log_warning("method is too long: %zu", strlen(method));
		return 400;
	} else {
		strncpy(client->request.method, method, sizeof(client->request.method));
	}

	if (strlen(path) >= sizeof(client->request.pathbuf)) {
		log_warning("path is too long: %zu", strlen(path));
		return 400;
	} else {
		strncpy(client->request.pathbuf, path, sizeof(client->request.pathbuf));
	}
    
    if ((err = url_parse(&client->request.url, client->request.pathbuf))) {
        log_warning("url_parse: %s", client->request.pathbuf);
        return 400;
    }

    if (client->request.url.scheme || client->request.url.host || client->request.url.port) {
        log_warning("request url includes extra parts: scheme=%s host=%s port=%s",
                client->request.url.scheme, client->request.url.host, client->request.url.port);
        return 400;
    }

    // mark as read
    client->request.request = true;
	
    log_info("%s %s %s", method, path, version);

    if (strcasecmp(version, "HTTP/1.0") == 0) {
        client->request.http11 = false;

        // implicit Connection: close
        client->response.close = true;

    } else if (strcasecmp(version, "HTTP/1.1") == 0) {
        client->request.http11 = true;

    } else {
        log_warning("unknown request version: %s", version);
    }

	return 0;
}

int server_request_header (struct server_client *client, const char **namep, const char **valuep)
{
	int err;

    if (!client->request.request) {
        log_fatal("premature read of request headers before request line");
        return -1;
    }

    if (client->request.headers) {
        log_warning("request headers have already been read");
        
        // ignore..
        return 1;
    }

	if ((err = http_read_header(client->http, namep, valuep)) < 0) {
        log_warning("http_read_header");
        return err;
    }

    if (err) {
        log_debug("end of headers");
        client->request.headers = true;
        return 1;
    }

    // mark as having read some headers
    client->request.header = true;

	log_info("\t%20s : %s", *namep, *valuep);

	if (strcasecmp(*namep, "Content-Length") == 0) {
		if (sscanf(*valuep, "%zu", &client->request.content_length) != 1) {
			log_warning("invalid content_length: %s", *valuep);
			return 400;
		}

		log_debug("content_length=%zu", client->request.content_length);

	} else if (strcasecmp(*namep, "Host") == 0) {
		if (strlen(*valuep) >= sizeof(client->request.hostbuf)) {
			log_warning("host is too long: %zu", strlen(*valuep));
			return 400;
		} else {
			strncpy(client->request.hostbuf, *valuep, sizeof(client->request.hostbuf));
		}
        
        // TODO: parse :port?
        client->request.url.host = client->request.hostbuf;

	} else if (strcasecmp(*namep, "Connection") == 0) {
        if (strcasecmp(*valuep, "close") == 0) {
            log_debug("using connection-close");

            client->response.close = true;

        } else if (strcasecmp(*valuep, "keep-alive") == 0) {
            /* Used by some HTTP/1.1 clients, apparently to request persistent connections.. */
            log_debug("explicitly not using connection-close");

            client->response.close = false;

        } else {
            log_warning("unknown connection header: %s", *valuep);
        }
    }
	
	return 0;
}

int server_request_file (struct server_client *client, int fd)
{
	int err;

    if (!client->request.headers) {
        log_fatal("read request body without reading headers!?");
        return -1;
    }

    if (client->request.body) {
        log_fatal("re-reading request body...");

        // XXX: ignore..?
        return -1;
    }

	// TODO: Transfer-Encoding?
	if (!client->request.content_length) {
		log_debug("no request body given");
		return 411;
	}
		
	if (((err = http_read_file(client->http, fd, client->request.content_length)))){
		log_warning("http_read_file");
		return err;
	}

    client->request.body = true;
	
	return 0;
}

int server_response (struct server_client *client, enum http_status status, const char *reason)
{
    int err;

	if (client->response.status) {
		log_fatal("attempting to re-send status: %u", status);
		return -1;
	}

    const char *version = client->request.http11 ? "HTTP/1.1" : "HTTP/1.0";

	log_info("%s %u %s", version, status, reason);

	client->response.status = status;

	if ((err = http_write_response(client->http, version, status, reason))) {
		log_error("failed to write response line");
		return err;
	}

    // custom headers
    struct server_header *header;

    TAILQ_FOREACH(header, &client->server->headers, server_headers) {
        if ((err = http_write_header(client->http, header->name, "%s", header->value))) {
            log_error("failed to write response header");
            return -1;
        }
    }
	
	return 0;
}

int server_response_header (struct server_client *client, const char *name, const char *fmt, ...)
{
	int err;

	if (!client->response.status) {
		log_fatal("attempting to send headers without status: %s", name);
		return -1;
	}

	if (client->response.headers) {
		log_fatal("attempting to re-send headers");
		return -1;
	}

	va_list args;

	log_info("\t%20s : %s", name, fmt);

	client->response.header = true;

	va_start(args, fmt);
	err = http_write_headerv(client->http, name, fmt, args);
	va_end(args);
	
	if (err) {
		log_error("failed to write response header line");
		return -1;
	}

	return 0;
}

int server_response_headers (struct server_client *client)
{
	client->response.headers = true;

	if (http_write_headers(client->http)) {
		log_error("failed to write end-of-headers");
		return -1;
	}

	return 0;
}

int server_response_file (struct server_client *client, int fd, size_t content_length)
{
	int err;
    
    if (content_length) {
        log_debug("using content-length");

        if ((err = server_response_header(client, "Content-Length", "%zu", content_length)))
            return err;
    } else {
        log_debug("using connection close");

        client->response.close = true;

        if ((err = server_response_header(client, "Connection", "close")))
            return err;
    }
	
	// headers
	if ((err = server_response_headers(client))) {
		return err;
	}

	// body
	if (client->response.body) {
		log_fatal("attempting to re-send body");
		return -1;
	}

	client->response.body = true;

	if (http_write_file(client->http, fd, content_length)) {
		log_error("http_write_file");
		return -1;
	}

	return 0;
}

int server_response_print (struct server_client *client, const char *fmt, ...)
{
	va_list args;
	int err = 0;

	if (!client->response.status) {
		log_fatal("attempting to send response body without status");
		return -1;
	}

	if (!client->response.headers) {
        // use chunked transfer-encoding for HTTP/1.1, and close connection for HTTP/1.0
        if (client->request.http11) {
            log_debug("using chunked transfer-encoding");

            client->response.chunked = true;

            err |= server_response_header(client, "Transfer-Encoding", "chunked");
            err |= server_response_headers(client);
        } else {
            log_debug("using connection close");

            client->response.close = true;

            err |= server_response_header(client, "Connection", "close");
            err |= server_response_headers(client);
        }
	}

	if (err)
		return err;
	
	// body
	client->response.body = true;

	va_start(args, fmt);
    if (client->response.chunked) {
        err = http_vprint_chunk(client->http, fmt, args);
    } else {
        err = http_vwrite(client->http, fmt, args);
    }
	va_end(args);

	if (err) {
		log_warning("http_write");
		return err;
	}

	return 0;
}

int server_response_redirect (struct server_client *client, const char *host, const char *fmt, ...)
{
	char path[HTTP_PATH_MAX];
	int ret;
	va_list args;

	va_start(args, fmt);
	ret = vsnprintf(path, sizeof(path), fmt, args);
	va_end(args);

	if (ret < 0) {
		log_perror("vsnprintf");
		return -1;
	} else if (ret >= sizeof(path)) {
		log_warning("truncated redirect path: %d", ret);
		return -1;
	}

	// auto
	if (!host) {
		host = client->request.url.host;
	}

	if ((
				server_response(client, 301, NULL)
			||	server_response_header(client, "Location", "http://%s/%s", host, path)
			||	server_response_headers(client)
	))
		return -1;

	return 0;
}

int server_response_error (struct server_client *client, enum http_status status, const char *reason, const char *detail)
{
    int err = 0;

    if (!reason)
		reason = http_status_str(status);

    err |= server_response(client, status, reason);
    err |= server_response_header(client, "Content-Type", "text/html");
    err |= server_response_print(client, "<html><head><title>HTTP %d %s</title></head><body>\n", status, reason);
    err |= server_response_print(client, "<h1>HTTP %d %s</h1>", status, reason);
    
    if (detail)
        err |= server_response_print(client, "<p>%s</p>", detail);

    err |= server_response_print(client, "</body></html>\n");

    return err;
}

/*
 * Read, process and respond to one request.
 *
 * Return <0 on internal error, 0 on success with persistent connection, 1 on success with cconnection-close.
 */
int server_client_request (struct server *server, struct server_client *client)
{
	struct server_handler *handler = NULL;
	enum http_status status = 0;
	int err;

	// request
	if ((err = server_request(client)) < 0) {
		goto error;

	} else if (err == 1) {
        // EOF
        return 1;

    } else if (err) {
        goto error;
    } 

	// handler 
	if ((err = server_lookup_handler(server, client->request.method, client->request.url.path, &handler)) < 0) {
		goto error;

	} else if (err) {
		handler = NULL;

	} else {
		if ((err = handler->request(handler, client, client->request.method, &client->request.url))) {
            log_warning("handler failed with %d", err);
        }
	}

    // headers?
    if (!client->request.headers) {
        const char *header, *value;

        log_debug("reading remaining headers...");

        // read remaining headers, in case they contain anything relevant for the error response
        // don't clobber err!
        while (!server_request_header(client, &header, &value)) {

        }
    }

    // body?
    // TODO: needs better logic for when a request contains a body?
    if (!client->request.body && client->request.content_length) {
        // force close, as pipelining will fail
        // we don't want to wait for the entire request body to upload before failing the request...
        log_debug("ignoring client request body");

        client->response.close = true;
    }

error:	
	// response
	if (err < 0) {
        // sock send/recv error or timeout, or other internal error
        // abort without response
        log_warning("aborting request without response");
        return err;

	} else if (err > 0) {
		status = err;
        log_debug("failing request with response %d", status);

	} else if (client->response.status) {
		status = 0;
        log_debug("request handler sent response %d", client->response.status);

	} else {
		log_warning("status not sent, defaulting to 200");
		status = 200;
	}
	
    // response line
	if (status && client->response.status) {
		log_warning("status %u already sent, should be %u", client->response.status, status);

	} else if (status) {
        // send full response
		if (server_response_error(client, status, NULL, NULL)) {
			log_warning("failed to send response status");
			err = -1;
		}
	}
	
	// headers
	if (!client->response.headers) {
        // end-of-headers
		if (server_response_headers(client)) {
			log_warning("failed to end response headers");
			err = -1;
		}
	}

    // entity
    if (client->response.chunked) {
        // end-of-chunks
        if ((err = http_write_chunks(client->http))) {
            log_warning("failed to end response chunks");
            err = -1;
        }
    }

    // persistent connection?
    if (client->response.close) {
        return 1;

    } else {
        return 0;
    }

	return err;
}

/*
 * Handle one client.
 */
void server_client_task (void *ctx)
{
    struct server_client *client = ctx;
    int err;

    // set idle timeouts
    tcp_read_timeout(client->tcp, &SERVER_READ_TIMEOUT);
    tcp_write_timeout(client->tcp, &SERVER_WRITE_TIMEOUT);

    // handle multiple requests
    while (true) {
        // reset request/response state...
        client->request = (struct server_request) { };
        client->response = (struct server_response) { };

        if ((err = server_client_request(client->server, client)) < 0) {
            log_warning("server_client_request");
            goto error;
        }

        if (err) {
            log_debug("end of client requests");
            break;
        }
    }

error:
	if (client->http)
		http_destroy(client->http);
    
    // TODO: clean close vs reset?
    tcp_destroy(client->tcp);

    free(client);
}

int server_client (struct server *server, struct tcp *tcp)
{
	struct server_client *client;
    int err = 0;

    if (!(client = calloc(1, sizeof(*client)))) {
        log_perror("calloc");
		goto error;
    }

    client->server = server;
    client->tcp = tcp;

	if ((err = http_create(&client->http, tcp_read_stream(tcp), tcp_write_stream(tcp)))) {
		log_perror("http_create");
		goto error;
	}

    if ((err = event_start(server->event_main, server_client_task, client))) {
        log_perror("event_start");
        goto error;
    }

    return 0;

error:
    if (client) {
        if (client->http)
            http_destroy(client->http);

        free(client);
    }
    
    tcp_destroy(tcp);

    return -1;
}

void server_listen_task (void *ctx)
{
    struct server_listen *listen = ctx;

    struct tcp *tcp;
    int err;

    while (true) {
        if ((err = tcp_server_accept(listen->tcp, &tcp))) {
            log_fatal("tcp_server_accept");
            break;
        }
        
        if ((err = server_client(listen->server, tcp))) {
            log_warning("server_client");
        }
    }

    tcp_server_destroy(listen->tcp);
    free(listen);
}

int server_listen (struct server *server, const char *host, const char *port)
{
    struct server_listen *listen;

    if (!(listen = calloc(1, sizeof(*listen)))) {
        log_perror("calloc");
        return -1;
    }

    listen->server = server;

    if (tcp_server(server->event_main, &listen->tcp, host, port)) {
        log_warning("tcp_server");
        goto error;
    }

    if (event_start(server->event_main, server_listen_task, listen)) {
        log_warning("event_start");
        goto error;
    }

    return 0;

error:
    if (listen->tcp)
        tcp_server_destroy(listen->tcp);

    free(listen);

    return -1;
}

void server_destroy (struct server *server)
{
    // TODO: listens

	// handlers
    struct server_handler_item *h;

    while ((h = TAILQ_FIRST(&server->handlers))) {
        TAILQ_REMOVE(&server->handlers, h, server_handlers);
        free(h);
    }

    // headers
    struct server_header *sh;

    while ((sh = TAILQ_FIRST(&server->headers))) {
        TAILQ_REMOVE(&server->headers, sh, server_headers);
        free(sh->name);
        free(sh->value);
        free(sh);
    }

	free(server);
}
