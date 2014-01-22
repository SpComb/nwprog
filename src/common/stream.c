#include "common/stream.h"

#include "common/log.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int stream_create (const struct stream_type *type, struct stream **streamp, size_t size, void *ctx)
{
    struct stream *stream;

    if (!(stream = calloc(1, sizeof(*stream)))) {
        log_perror("calloc");
        return -1;
    }

    if (stream_init(type, stream, size, ctx))
        goto error;

    *streamp = stream;
    return 0;

error:
    free(stream);
    return -1;
}

int stream_init (const struct stream_type *type, struct stream *stream, size_t size, void *ctx)
{
    // buffer
    if (!(stream->buf = malloc(size))) {
        log_perror("malloc %zu", size);
        return -1;
    }
    
    stream->type = type;
    stream->size = size;
    stream->length = 0;
    stream->offset = 0;
    stream->ctx = ctx;

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
 *
 * Returns 1 on EOF.
 */
int _stream_read (struct stream *stream)
{
    int err;

    // XXX: make room if needed
    if ((err = _stream_shift(stream)))
        return err;
    
    // fill up
    size_t len = stream->size - stream->length;

    if ((err = stream->type->read(stream->buf + stream->length, &len, stream->ctx))) {
        log_pwarning("stream-read");
        return err;
    }

    if (!len) {
        log_debug("eof");
        return 1;
    }

    stream->length += len;

    return 0;
}

int _stream_write_direct (struct stream *stream, const char *buf, size_t size)
{
    while (size) {
        size_t len = size;

        if (stream->type->write(buf, &len, stream->ctx)) {
            log_pwarning("stream-write");
            return -1;
        }

        if (!len) {
            log_debug("eof");
            return 1;
        }

        buf += len;
        size -= len;
    }

    return 0;
}

int _stream_write (struct stream *stream)
{
    while (stream->offset < stream->length) {
        size_t len = stream->length - stream->offset;

        if (stream->type->write(stream->buf + stream->offset, &len, stream->ctx)) {
            log_pwarning("stream-write");
            return -1;
        }

        if (!len) {
            log_debug("eof");
            return 1;
        }

        stream->offset += len;
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
		
		if (err)
			break;
    }

    *bufp = stream->buf + stream->offset;

    if (*sizep && stream->offset + *sizep < stream->length) {

    } else if (stream->length > stream->offset) {
        *sizep = stream->length - stream->offset;
    } else {
		// EOF
		return 1;
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
		// XXX: should we return the last line on EOF, or expect a trailing \r\n?
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
