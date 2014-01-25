#include "common/stream.h"

#include "common/log.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Read buffer start, for reading new data into the stream.
 */
static inline char * stream_readbuf_ptr (struct stream *stream)
{
    return stream->buf + stream->length;
}

/*
 * Read buffer size, for reading new data into the stream.
 */
static inline size_t stream_readbuf_size (struct stream *stream)
{
    return stream->size - stream->length;
}

/*
 * Write buffer start, for reading old data from the stream.
 */
static inline char * stream_writebuf_ptr (struct stream *stream)
{
    return stream->buf + stream->offset;
}

/*
 * Write buffer size, for reading old data from the stream.
 */
static inline size_t stream_writebuf_size (struct stream *stream)
{
    return stream->length - stream->offset;
}

/*
 * Write buffer end, for reading old data from the stream.
 */
static inline char * stream_writebuf_end (struct stream *stream)
{
    return stream->buf + stream->length;
}

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
 * Mark given readbuf bytes as valid.
 */
inline static void stream_read_mark (struct stream *stream, size_t size)
{
    stream->length += size;
}

/*
 * Mark given writebuf bytes as consumed.
 */
inline static void stream_write_mark (struct stream *stream, size_t size)
{
    stream->offset += size;
}

/*
 * Clean out marked write buffer, making more room for the read buffer.
 *
 * Returns -1 if the stream buffer is full.
 */
int _stream_clear (struct stream *stream)
{
    if (!(stream->offset || stream->length < stream->size)) {
        log_warning("stream write buffer is full, no room for read");
        return -1;
    }

    memmove(stream->buf, stream->buf + stream->offset, stream->length - stream->offset);

    stream->length -= stream->offset;
    stream->offset = 0;

    return 0;
}

/*
 * Read into stream from fd. There must be room available for the read buffer.
 *
 * This function is guaranteed to make progress.
 *
 * Returns 1 on EOF. There may still be data in the buffer.
 */
int _stream_read (struct stream *stream)
{
    int err;
 
    // fill up
    size_t size = stream_readbuf_size(stream);

    if ((err = stream->type->read(stream_readbuf_ptr(stream), &size, stream->ctx)) < 0) {
        log_pwarning("stream-read");
        return err;
    }

	if (!size) {
        log_debug("eof");
        return 1;
    }

	stream_read_mark(stream, size);

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
    size_t size = stream_writebuf_size(stream);

    if (!size) {
        // XXX
        log_warning("write without anything to write");
        return 0;
    }

    if (stream->type->write(stream_writebuf_ptr(stream), &size, stream->ctx)) {
        return -1;
    }

    if (!size) {
        log_debug("eof");
        return 1;
    }

    stream_write_mark(stream, size);

    return 0;
}

int stream_read (struct stream *stream, char **bufp, size_t *sizep)
{
    int err;
    
    // make room if needed
    if ((err = _stream_clear(stream)))
        return err;

    // until we have the request amount of data, or any data, or EOF
    while (stream->length < stream->offset + *sizep) {
        if ((err = _stream_read(stream)) < 0)
            return err;
		
		if (err)
			break;
    }

    *bufp = stream->buf + stream->offset;

    if (*sizep && stream->offset + *sizep < stream->length) {
        // have full requested size

    } else if (stream->length > stream->offset) {
        // have partial requested size, or however much available
        *sizep = stream->length - stream->offset;
    } else {
		// EOF
		return 1;
	}

    // consumed
    stream->offset += *sizep;

    return 0;
}

int stream_read_line (struct stream *stream, char **linep)
{
    char *c;
    int err;
    
    // make room if needed
    if ((err = _stream_clear(stream)))
        return err;

    while (true) {
        // scan for \r\n
        for (c = stream_writebuf_ptr(stream); c < stream_writebuf_end(stream); c++) {
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
    *linep = stream_writebuf_ptr(stream);

    // past end of line
    stream_write_mark(stream, c - stream_writebuf_ptr(stream) + 1);

    return 0;
}

int stream_read_file (struct stream *stream, int fd, size_t *sizep)
{
    int err;
    ssize_t ret;

    // make room if needed
    if ((err = _stream_clear(stream)))
        return err;

	// read in anything available
	if ((err = _stream_read(stream)) < 0)
		return err;
	
	// detect eof
	if (err > 0 && !stream_writebuf_size(stream)) {
		log_debug("eof");
		return 1;
	}

    size_t size = stream_writebuf_size(stream);

    if (*sizep && *sizep < size)
        // limit
        size = *sizep;

    if ((ret = write(fd, stream_writebuf_ptr(stream), size)) < 0) {
        log_perror("write %d", fd);
        return -1;
    }

    if (!ret) {
        log_debug("write: eof");
        return 1;
    }

    stream_write_mark(stream, ret);

    // update
    *sizep = ret;
    
    // TODO: cleanup

    return 0;
}

/*
 * Flush any pending writes.
 */
int stream_flush (struct stream *stream)
{
    int err;

    // empty write buffer
    while (stream_writebuf_size(stream) > 0) {
        if ((err = _stream_write(stream)))
            return err;
    }

    // drop consumed data from write buffer
    if ((err = _stream_clear(stream)) < 0)
        return err;

    return 0;
}

int stream_write (struct stream *stream, const char *buf, size_t size)
{
    int err;

    // our write buffer must be empty, since _stream_write_direct will bypass it
    if ((err = stream_flush(stream)))
        return err;

    return _stream_write_direct(stream, buf, size);
}

int stream_vprintf (struct stream *stream, const char *fmt, va_list args)
{
    int ret, err;

    if ((ret = vsnprintf(stream_readbuf_ptr(stream), stream_readbuf_size(stream), fmt, args)) < 0) {
        log_perror("snprintf");
        return -1;
    }
    
    if (ret >= stream_readbuf_size(stream))
        // full
        return 1;
    
    stream_read_mark(stream, ret);
    
    // TODO: write buffering
    if ((err = stream_flush(stream)))
        return err;

    return 0;
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

/*
 * Fallback for sendfile.
 */
int _stream_write_file (struct stream *stream, int fd, size_t *sizep)
{
    int err;
    ssize_t ret;

    if ((err = _stream_clear(stream)))
        return err;

    // read into readbuf
    size_t size = stream_readbuf_size(stream);

    if (*sizep && *sizep < size)
        // limit
        size = *sizep;

    if ((ret = read(fd, stream_readbuf_ptr(stream), size)) < 0) {
        log_perror("read");
        return -1;
    }

    if (!ret) {
        log_debug("read: eof");
        return 1;
    }

    stream_read_mark(stream, ret);

    // update
    *sizep = ret;

    // write out
    if ((err = _stream_write(stream)))
        return err;

    return 0;
}

int stream_write_file (struct stream *stream, int fd, size_t *sizep)
{
    int err;

    if (!stream->type->sendfile)
        // fallback
        return _stream_write_file(stream, fd, sizep);

    // our write buffer must be empty, since sendfile will bypass it
    if ((err = stream_flush(stream)))
        return err;

    if ((err = stream->type->sendfile(fd, sizep, stream->ctx)))
        return err;

    return 0;
}

void stream_destroy (struct stream *stream)
{
    free(stream->buf);
    free(stream);
}
