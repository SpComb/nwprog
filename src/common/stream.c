#include "common/stream.h"

#include "common/log.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int stream_create (struct stream **streamp, int fd, size_t size)
{
    struct stream *stream;

    if (!(stream = calloc(1, sizeof(*stream)))) {
        log_perror("calloc");
        return -1;
    }

    if (stream_init(stream, fd, size))
        goto error;

    *streamp = stream;
    return 0;

error:
    free(stream);
    return -1;
}

int stream_init (struct stream *stream, int fd, size_t size)
{
    // buffer
    if (!(stream->buf = malloc(size))) {
        log_perror("malloc %zu", size);
        return -1;
    }
    
    stream->fd = fd;
    stream->size = size;
    stream->length = 0;
    stream->offset = 0;

    return 0;
}

/*
 * Make space in stream for more data.
 *
 * Returns 1 if the stream buffer is full.
 */
int _stream_shift (struct stream *stream)
{
    if (stream->offset) {
        memmove(stream->buf, stream->buf + stream->offset, stream->length - stream->offset);

        stream->length -= stream->offset;
        stream->offset = 0;
    }

    return 0;
}

/*
 * Read into stream from fd.
 */
int _stream_read (struct stream *stream)
{
    ssize_t ret;
    int err;

    // XXX: make room if needed
    if ((err = _stream_shift(stream)))
        return err;
    
    // fill up
    if ((ret = read(stream->fd, stream->buf + stream->length, stream->size - stream->length)) < 0) {
        log_pwarning("read");
        return -1;
    }

    if (!ret) {
        log_warning("eof");
        return 1;
    }

    stream->length += ret;

    return 0;
}

int _stream_write_direct (struct stream *stream, const char *buf, size_t size)
{
    ssize_t ret;

    while (size) {
        if ((ret= write(stream->fd, buf, size)) < 0) {
            log_pwarning("write");
            return -1;
        }

        if (!ret) {
            log_warning("eof");
            return 1;
        }

        buf += ret;
        size -= ret;
    }

    return 0;
}

int _stream_write (struct stream *stream)
{
    ssize_t ret;

    while (stream->offset < stream->length) {
        if ((ret = write(stream->fd, stream->buf + stream->offset, stream->length - stream->offset)) < 0) {
            log_pwarning("write");
            return -1;
        }

        if (!ret) {
            log_warning("eof");
            return 1;
        }

        stream->offset += ret;
    }

    return 0;
}

int stream_read (struct stream *stream, char **bufp, size_t *sizep)
{
    int err;
    
    // do readz
    while (stream->length < stream->offset + *sizep) {
        if ((err = _stream_read(stream)) < 0)
            return err;
    }

    *bufp = stream->buf + stream->offset;

    if (*sizep && stream->offset + *sizep < stream->length) {

    } else {
        *sizep = stream->length - stream->offset;
    }
        
    stream->offset += *sizep;

    return 0;
}

int stream_read_line (struct stream *stream, char **linep)
{
    char *c;
    int err;
    
    while (true) {
        // scan for \r\n
        for (c = stream->buf + stream->offset; c < stream->buf + stream->length; c++) {
            if (*c == '\r') {
                *c = '\0';
            } else if (*c == '\n') {
                *c = '\0';
                goto out;
            }
        }

        // needs moar bytez in mah buffers
        if ((err = _stream_read(stream)))
            return err;
    }

out:
    // start of line
    *linep = stream->buf + stream->offset;

    // end of line
    stream->offset = c - stream->buf + 1;

    return 0;
}

int stream_write (struct stream *stream, const char *buf, size_t size)
{
    // XXX: we assume there is no write buffer
    return _stream_write_direct(stream, buf, size);
}

int stream_vprintf (struct stream *stream, const char *fmt, va_list args)
{
    int ret, err;

    if ((err = _stream_shift(stream)))
        return err;

    if ((ret = vsnprintf(stream->buf + stream->length, stream->size - stream->length, fmt, args)) < 0) {
        log_perror("snprintf");
        return -1;
    }

    if (stream->length + ret > stream->size) {
        // full
        return 1;
    }

    stream->length += ret;

    return _stream_write(stream);
}

int stream_printf (struct stream *stream, const char *fmt, ...)
{
    va_list args;
    int ret;
    
    va_start(args, fmt); 
    ret = stream_vprintf(stream, fmt, args);
    va_end(args);

    return ret;
}

void stream_destroy (struct stream *stream)
{
    free(stream->buf);
    free(stream);
}
