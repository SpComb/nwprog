#ifndef STREAM_H
#define STREAM_H

#include <stdlib.h>
#include <stdarg.h>

/*
 * SOCK_STREAM support..
 */
struct stream;

int stream_create (struct stream **streamp, int fd, size_t size);

/*
 * Read binary data from the stream.
 *
 * *sizep may be passed as 0 to read as much data as available, or a maximum amount to return.
 *
 * The amount of data available is returned in *sizep, and may be less on EOF.
 */
int stream_read (struct stream *stream, char **bufp, size_t *sizep);

/*
 * Read one line from the stream, returning a pointer to the NUL-terminated line.
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
