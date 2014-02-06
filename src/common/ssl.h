#ifndef WITH_SSL
#error building without SSL support
#endif

#ifndef SSL_H
#define SSL_H

/*
 * SSL connection support *g*
 */
struct ssl_main;
struct ssl;

// XXX
#define SSL_STREAM_SIZE 1024

/*
 * Initialize context for SSL connections.
 */
int ssl_main_create (struct ssl_main **mainp);

/*
 * Open a new SSL client connection to a SSL server adressed by host:port.
 */
int ssl_client (struct ssl_main *ssl_main, struct ssl **sslp, const char *host, const char *port);

/*
 * IO streams.
 */
int ssl_sock (struct ssl *ssl);
struct stream * ssl_read_stream (struct ssl *ssl);
struct stream * ssl_write_stream (struct ssl *ssl);

/*
 * Release all associated resources.
 */
void ssl_destroy (struct ssl *ssl);

#endif
