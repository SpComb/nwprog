#include "common/http.h"

#include "common/log.h"
#include "common/parse.h"
#include "common/util.h"

// vsnprintf with -gnu99
#define _POSIX_C_SOURCE 200112L

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct http {
    struct stream *read, *write;

	const char *version;
};

const char * http_status_str (enum http_status status)
{
	switch (status) {
		case 200:	return "OK";

		case 400:	return "Bad Request";
		case 403:   return "Forbidden";
		case 404:	return "Not Found";
		case 405:   return "Method Not Allowed";
		case 411:   return "Length Required";
		case 413:   return "Request Entity Too Large";
		case 414:   return "Request-URI Too Long";

		case 500:	return "Internal Server Error";

		// hrhr
		default:	return "Unknown Response Status";
	}
}

int http_create (struct http **httpp, struct stream *read, struct stream *write)
{
	struct http *http = NULL;

	if (!(http = calloc(1, sizeof(*http)))) {
		log_perror("calloc");
		goto error;
	}

    http->read = read;
    http->write = write;
	http->version = HTTP_VERSION;
	
	// ok
	*httpp = http;

	return 0;

error:
	if (http)
		free(http);

	return -1;
}

int http_write_buf (struct http *http, const char *buf, size_t *lenp)
{
    return stream_write(http->write, buf, *lenp);
}

int http_vwrite (struct http *http, const char *fmt, va_list args)
{
    return stream_vprintf(http->write, fmt, args);
}

int http_writef (struct http *http, const char *fmt, ...)
{
	va_list args;
	int err;

	va_start(args, fmt);
    err = stream_vprintf(http->write, fmt, args);
	va_end(args);
	
	return err;
}

/*
 * Write a line to the HTTP socket.
 */
static int http_write_line (struct http *http, const char *fmt, ...)
{
	va_list args;
	int err;
	
	va_start(args, fmt);
    err = stream_vprintf(http->write, fmt, args);
	va_end(args);
	
	if (err)
		return err;

	if ((err = stream_printf(http->write, "\r\n")))
		return err;

	return 0;
}

/*
 * Read arbitrary data from the connection.
 *
 * Returns 0 on success, <0 on error, >0 on EOF.
 */
static int http_read (struct http *http, char *buf, size_t *lenp)
{
    char *inbuf;
    int err;

    if ((err = stream_read(http->read, &inbuf, lenp)))
        return err;
    
    log_debug("%zu", *lenp);

    memcpy(buf, inbuf, *lenp);

    return 0;
}

/*
 * Read one line from the connection, returning a pointer to the (stripped) line in **linep.
 *
 * The returned pointer remains valid until the next http_read_line call, and may be modified.
 *
 * Returns 0 on success, <0 on error, >0 on EOF.
 */
static int http_read_line (struct http *http, char **linep)
{
    int err;

    if ((err = stream_read_line(http->read, linep)))
        return err;

    log_debug("%s", *linep);

    return 0;
}

int http_parse_header (char *line, const char **headerp, const char **valuep)
{
	enum state { START, HEADER, SEP_PRE, SEP, SEP_POST, VALUE, END, FOLD_VALUE };
	struct parse parsing[] = {
		{ START,		' ',	FOLD_VALUE	},
		{ START,		'\t',	FOLD_VALUE	},
		{ START,		-1,		HEADER,		PARSE_KEEP		},

		{ HEADER,		' ',	SEP,		PARSE_STRING,	.parse_string = headerp		},
		{ HEADER,		'\t',	SEP,		PARSE_STRING,	.parse_string = headerp		},
		{ HEADER,		':',	SEP_POST,	PARSE_STRING,	.parse_string = headerp		},
		
		{ SEP,			' ',	SEP			},
		{ SEP,			'\t',	SEP			},
		{ SEP,			':',	SEP_POST	},

		{ SEP_POST,		' ',	SEP_POST	},
		{ SEP_POST,		'\t',	SEP_POST	},
		{ SEP_POST,		-1,		VALUE,		PARSE_KEEP 		},
		
		{ VALUE,		0,		END,		PARSE_STRING,	.parse_string = valuep		},
		
		/*For folded headers, we leave headerp as-is. */
		{ FOLD_VALUE,	' ',	FOLD_VALUE	},
		{ FOLD_VALUE,	0,		END,		PARSE_STRING,	.parse_string = valuep		},

		{ }
	};
	int err;

	// parse
	if ((err = parse(parsing, line, START)) != END)
		return 400;

	return 0;
}

/* Client request writing */
int http_write_request (struct http *http, const char *method, const char *fmt, ...)
{
	va_list args;
	int err;

	if ((err = http_writef(http, "%s ", method)))
		return err;

	va_start(args, fmt);
	err = http_vwrite(http, fmt, args);
	va_end(args);
	
	if (err)
		return err;

	if ((err = http_writef(http, " %s\r\n", http->version)))
		return err;

	return 0;
}

int http_write_response (struct http *http, unsigned status, const char *reason)
{
	if (!reason) {
		reason = http_status_str(status);
	}

	return http_write_line(http, "%s %u %s", HTTP_VERSION, status, reason);
}

int http_write_headerv (struct http *http, const char *header, const char *fmt, va_list args)
{
	int err;
	
	if ((err = http_writef(http, "%s: ", header)))
		return err;

	if ((err = http_vwrite(http, fmt, args)))
		return err;
	
	if ((err = http_writef(http, "\r\n")))
		return err;

	return 0;
}

int http_write_header (struct http *http, const char *header, const char *fmt, ...)
{
	va_list args;
	int err;

	va_start(args, fmt);
	err = http_write_headerv(http, header, fmt, args);
	va_end(args);

	return err;
}

int http_write_headers (struct http *http)
{
	return http_write_line(http, "");
}


int http_write_file (struct http *http, FILE *file, size_t content_length)
{
	char buf[512];
	size_t ret, len = sizeof(buf);

	while (content_length) {
		len = sizeof(buf);

		log_debug("content_length: %zu", content_length);
		
		// cap to expected dat
		if (content_length < len)
			len = content_length;
		
		// read block
		if ((ret = fread(buf, 1, len, file)) < 0) {
			log_pwarning("fread");
			return -1;

		} else if (!ret) {
			log_debug("EOF");
			break;
		} else {
			log_debug("fread: %zu", ret);
			len = ret;
		}

		// sanity-check
		if (len <= content_length) {
			content_length -= len;
		} else {
			log_fatal("BUG: len=%zu > content_length=%zu", len, content_length);
			return -1;
		}

		// copy to request
		char *bufp = buf;
		size_t buflen = len;
		
		while (len) {
			if (http_write_buf(http, bufp, &buflen)) {
				log_error("error writing request body");
				return -1;
			}

			log_debug("http_write: %zu", buflen);

			bufp += buflen;
			len -= buflen;
		}
	}

	if (content_length) {
		log_warning("premature EOF: %zu", content_length);
		return 1;
	}

	return 0;
}

int http_parse_request (char *line, const char **methodp, const char **pathp, const char **versionp)
{
	enum state { START, METHOD, PATH, VERSION, END };
	struct parse parsing[] = {
		{ START, 	' ', 	-1			},
		{ START,	-1,		METHOD,		PARSE_KEEP 		},

		{ METHOD,	' ',	PATH,		PARSE_STRING,	.parse_string = methodp		},
		{ PATH,		' ', 	VERSION,	PARSE_STRING,	.parse_string = pathp		},
		{ VERSION,	'\0',	END,		PARSE_STRING,	.parse_string = versionp	},
		{ }
	};
	int err;

	// parse
	if ((err = parse(parsing, line, START)) != END)
		return 400;

	return 0;
}

int http_parse_response (char *line, const char **versionp, unsigned *statusp, const char **reasonp)
{
	enum state { START, VERSION, STATUS, REASON, END };
	struct parse parsing[] = {
		{ START, 	' ', 	-1			},
		{ START,	-1,		VERSION,	PARSE_KEEP 		},

		{ VERSION,	' ',	STATUS,		PARSE_STRING,	.parse_string = versionp	},
		{ STATUS,	' ', 	REASON,		PARSE_UINT,		.parse_uint	= statusp		},
		{ REASON,	'\0',	END,		PARSE_STRING,	.parse_string = reasonp		},
		{ }
	};
	int err;

	// parse
	if ((err = parse(parsing, line, START)) != END)
		return 400;

	return 0;
}

int http_read_request (struct http *http, const char **methodp, const char **pathp, const char **versionp)
{
	char *line;
	int err;

	if ((err = http_read_line(http, &line)))
		return err;

	return http_parse_request(line, methodp, pathp, versionp);
}

int http_read_response (struct http *http, const char **versionp, unsigned *statusp, const char **reasonp)
{
	char *line;
	int err;

	if ((err = http_read_line(http, &line)))
		return err;
	
	return http_parse_response(line, versionp, statusp, reasonp);
}

int http_read_header (struct http *http, const char **headerp, const char **valuep)
{
	char *line;
	int err;

	if ((err = http_read_line(http, &line)) < 0)
		return err;

	if (err) {
		// XXX: interpret EOF as end-of-headers?
		return 1;
	}
	
	if (!*line) {
		log_debug("end of headers");
		return 1;
	}

	return http_parse_header(line, headerp, valuep);
}

int http_read_raw (struct http *http, char *buf, size_t *lenp)
{
	return http_read(http, buf, lenp);
}

int http_read_file (struct http *http, FILE *file, size_t content_length)
{
	char buf[512];
	size_t len = sizeof(buf), ret;
	bool readall = (!content_length);
	int err;

	while (readall || content_length) {
		len = sizeof(buf);
		
		if (content_length) {
			log_debug("content_length: %zu", content_length);
			
			// cap to expected dat
			if (content_length < len)
				len = content_length;
		}

		// read block
		if ((err = http_read(http, buf, &len)) < 0) {
			return err;

		} else if (err) {
			// EOF
			break;

		} else {
			log_debug("read: %zu", len);
		}
		
		if (content_length) {
			// sanity-check
			if (len <= content_length) {
				content_length -= len;
			} else {
				log_fatal("BUG: len=%zu > content_length=%zu", len, content_length);
				return -1;
			}
		}

		// copy to stdout
		if (file && (ret = fwrite(buf, len, 1, file)) != 1) {
			log_pwarning("fwrite");
			return -1;
		}
		
		log_debug("write: %zu", len);
	}
	
	if (content_length) {
		log_warning("missing content: %zu", content_length);
		return 1;
	}

	return 0;
}

void http_destroy (struct http *http)
{
    free(http);
}
