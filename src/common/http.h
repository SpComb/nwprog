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
int http_client_request_start (struct http *http, const char *method, const char *path);
int http_client_request_start_path (struct http *http, const char *method, const char *fmt, ...);
int http_client_request_header (struct http *http, const char *header, const char *value);
int http_client_request_end (struct http *http);

/*
 * Read a HTTP response.
 */
int http_client_response_start (struct http *http, const char **versionp, const char **statusp, const char **reasonp);

/*
 * Read next header as {*headerp}: {*valuep}.
 *
 * In case of a folded header, *headerp is left as-is.
 *
 * Returns 1 on end-of-headers, 0 on header, <0 on error.
 */
int http_client_response_header (struct http *http, const char **headerp, const char **valuep);

#endif
