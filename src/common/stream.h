#ifndef STREAM_H
#define STREAM_H

#include <stdlib.h>
#include <stdarg.h>

/*
 * Buffered SOCK_STREAMs
 */
struct stream_type {
    int (*read)(char *buf, size_t *sizep, void *ctx);
    int (*write)(const char *buf, size_t *sizep, void *ctx);
};

struct stream {
    const struct stream_type *type;

    char *buf;
    size_t size, length, offset;

    void *ctx;
};

int stream_create (const struct stream_type *type, struct stream **streamp, size_t size, void *ctx);
int stream_init (const struct stream_type *type, struct stream *stream, size_t size, void *ctx);

/*
 * Read binary data from the stream.
 *
 * *sizep may be passed as 0 to read as much data as available, or a maximum amount to return.
 *
 * The amount of data available is returned in *sizep, and may be less on EOF.
 *
 * Returns 1 on EOF, <0 on error.
 */
int stream_read (struct stream *stream, char **bufp, size_t *sizep);

/*
 * Read one line from the stream, returning a pointer to the NUL-terminated line.
 *
 * Returns 1 on EOF, <0 on error.
 */
int stream_read_line (struct stream *stream, char **linep);

int stream_write (struct stream *stream, const char *buf, size_t size);
int stream_vprintf (struct stream *stream, const char *fmt, va_list args);
int stream_printf (struct stream *stream, const char *fmt, ...);

/*
 * Release all resources.
 */
void stream_destroy (struct stream *stream);

#endif
