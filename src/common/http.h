#ifndef HTTP_H
#define HTTP_H

struct http;

#define HTTP_VERSION "HTTP/1.0"

/*
 * Create a new HTTP connect using the given socket.
 */
int http_create (struct http **httpp, int sock);

/*
 * Send a HTTP request.
 */
int http_request_start (struct http *http, const char *method, const char *path);
int http_request_start_path (struct http *http, const char *method, const char *fmt, ...);
int http_request_header (struct http *http, const char *header, const char *value);
int http_request_end (struct http *http);

#endif
