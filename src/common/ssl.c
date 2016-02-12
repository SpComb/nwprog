/*
 * Experimental SSL client support.
 *
 * XXX:
 *      no server cert validation
 *
 * TODO:
 *      event/non-blocking support
 *      ssl_server support
 */

#include "common/ssl.h"

#include "common/log.h"
#include "common/stream.h"
#include "common/tcp.h"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <unistd.h>

#ifndef WITH_SSL
#error XXX: building common/ssl.o without WITH_SSL
#endif

struct ssl_main {
    SSL_CTX *ssl_ctx;
};

struct ssl {
    struct ssl_main *ssl_main;

    int sock;
    SSL *SSL;
    
    struct stream *read, *write;
};

/*
 * Return a string representation of the most recent SSL_* error.
 *
 * Must call SSL_load_error_strings() before.
 */
static const char * ssl_error_str ()
{
    return ERR_error_string(ERR_get_error(), NULL);
}

int ssl_main_create (struct ssl_main **ssl_mainp)
{
    struct ssl_main *ssl_main;

    // XXX
    const SSL_METHOD *ssl_method = SSLv23_method();

    // libssl init
    SSL_library_init();
    SSL_load_error_strings();

    if (!(ssl_main = calloc(1, sizeof(*ssl_main)))) {
        log_perror("calloc");
        return -1;
    }

    if (!(ssl_main->ssl_ctx = SSL_CTX_new(ssl_method))) {
        log_error("SSL_CTX_new: %s", ssl_error_str());
        goto error;
    }

    *ssl_mainp = ssl_main;

    return 0;

error:
    free(ssl_main);
    return -1;
}

/* Stream support */
int ssl_stream_read (char *buf, size_t *sizep, void *ctx)
{
    struct ssl *ssl = ctx;
    int ret;
    
    if ((ret = SSL_read(ssl->SSL, buf, *sizep)) < 0) {
        log_error("SSL_read: XXX");
        return -1;
    }

    if (!ret) {
        log_debug("eof: XXX");
        return 1;
    }

    *sizep = ret;

    return 0;
}

int ssl_stream_write (const char *buf, size_t *sizep, void *ctx)
{
    struct ssl *ssl = ctx;
    int ret;
    
    if ((ret = SSL_write(ssl->SSL, buf, *sizep)) < 0) {
        log_error("SSL_write: XXX");
        return -1;
    }

    if (!ret) {
        log_debug("eof: XXX");
        return 1;
    }

    *sizep = ret;

    return 0;
}

struct stream_type ssl_stream_type = {
    .read   = ssl_stream_read,
    .write  = ssl_stream_write,
};

int ssl_connect (struct ssl *ssl, const char *host, const char *port)
{
    int err;

    // TODO: event_main
    if ((err = tcp_connect(NULL, &ssl->sock, host, port))) {
        log_perror("tcp_connect %s:%s", host, port);
        return err;
    }

    if (SSL_set_fd(ssl->SSL, ssl->sock) != 1) {
        log_error("SSL_set_fd: %s", ssl_error_str());
        return -1;
    }

    if ((err = SSL_connect(ssl->SSL)) != 1) {
        switch (SSL_get_error(ssl->SSL, err)) {
            case SSL_ERROR_ZERO_RETURN:
                log_warning("SSL_connect: closed");
                return 1;

            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_CONNECT:
            case SSL_ERROR_WANT_ACCEPT:
            case SSL_ERROR_WANT_X509_LOOKUP:
                log_error("SSL_connect: nonblocking");
                return -1;

            case SSL_ERROR_SYSCALL:
                if (err) {
                    log_perror("SSL_connect");
                    return -1;
                } else {
                    log_warning("SSL_connect: eof");
                    return 1;
                }

            case SSL_ERROR_SSL:
                log_error("SSL_connect: %s", ssl_error_str());
                return -1;
        }

        log_error("SSL_connect: unknown error");
        return -1;
    }

    // log
    const char *ssl_version = SSL_get_version(ssl->SSL);
    const char *ssl_cipher = SSL_get_cipher_name(ssl->SSL);

    log_info("connected %s:%s (%s %s)", host, port, ssl_version, ssl_cipher);

    return 0;
}

int ssl_client (struct ssl_main *ssl_main, struct ssl **sslp, const char *host, const char *port)
{
    struct ssl *ssl = NULL;
    int err;

    if (!(ssl = calloc(1, sizeof(*ssl)))) {
        log_perror("calloc");
        return -1;
    }

    ssl->ssl_main = ssl_main;

    if (!(ssl->SSL = SSL_new(ssl_main->ssl_ctx))) {
        log_error("SSL_new: %s", ssl_error_str());
        err = -1;
        goto error;
    }

    // network connect + handshake
    if ((err = ssl_connect(ssl, host, port)))
        goto error;

    if ((err = stream_create(&ssl_stream_type, &ssl->read, SSL_STREAM_SIZE, ssl))) {
        log_error("stream_create read");
        goto error;
    }
    
    if ((err = stream_create(&ssl_stream_type, &ssl->write, SSL_STREAM_SIZE, ssl))) {
        log_error("stream_create write");
        goto error;
    }
    
    *sslp = ssl;

    return 0;

error:
    ssl_destroy(ssl);

    return err; 
}

int ssl_sock (struct ssl *ssl)
{
    return ssl->sock;
}

struct stream * ssl_read_stream (struct ssl *ssl)
{
    return ssl->read;
}

struct stream * ssl_write_stream (struct ssl *ssl)
{
    return ssl->write;
}

void ssl_destroy (struct ssl *ssl)
{
    if (ssl->sock)
        close(ssl->sock);

    if (ssl->SSL)
        SSL_free(ssl->SSL);

    free(ssl);
}
