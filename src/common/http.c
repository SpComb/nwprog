#include "common/http.h"

#include "common/log.h"
#include "common/parse.h"
#include "common/util.h"

// vsnprintf with -gnu99
#define _POSIX_C_SOURCE 200112L

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Maximum line length */
#define HTTP_LINE 1024

struct http {
	FILE *file;
	char buf[HTTP_LINE];

	const char *version;
};


int http_create (struct http **httpp, int fd)
{
	struct http *http = NULL;

	if (!(http = calloc(1, sizeof(*http)))) {
		log_perror("calloc");
		goto error;
	}

	if (!(http->file = fdopen(fd, "w+"))) {
		log_perror("fdopen");
		goto error;
	}

	http->version = HTTP_VERSION;
	
	// ok
	*httpp = http;

	return 0;

error:
	if (http)
		free(http);

	return -1;
}

static int http_write (struct http *http, const char *buf, size_t *lenp)
{
	size_t ret;

	if ((ret = fwrite(buf, 1, *lenp, http->file)) < 0) {
		log_pwarning("fwrite");
		return -1;
	}

	*lenp = ret;
	
	return 0;
}

static int http_vwrite (struct http *http, const char *fmt, va_list args)
{
	char buf[HTTP_LINE];
	int ret;

	if ((ret = vsnprintf(buf, sizeof(buf), fmt, args)) < 0)
		return -1;

	if (ret >= sizeof(buf)) {
		log_warning("overflow: %d", ret);
		return 1;
	}

	if (!ret) {
		log_debug("''");
		return 0;
	}
	
	log_debug("'%s'", strdump(buf));
	
	if ((ret = fwrite(buf, ret, 1, http->file)) < 0) {
		log_pwarning("fwrite");
		return -1;

	} else if (!ret) {
		log_pwarning("fwrite: EOF");
		return 1;
	}

	return 0;
}

static int http_writef (struct http *http, const char *fmt, ...)
{
	va_list args;
	int err;

	va_start(args, fmt);
	err = http_vwrite(http, fmt, args);
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
	err = http_vwrite(http, fmt, args);
	va_end(args);
	
	if (err)
		return err;

	if ((err = http_writef(http, "\r\n")))
		return err;

	return 0;
}

static int http_write_headerv (struct http *http, const char *header, const char *fmt, va_list args)
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

static int http_write_header (struct http *http, const char *header, const char *fmt, ...)
{
	va_list args;
	int err;
	
	va_start(args, fmt);
	err = http_write_headerv(http, header, fmt, args);
	va_end(args);

	return err;
}

/*
 * Read arbitrary data from the connection.
 *
 * Returns 0 on success, <0 on error, >0 on EOF.
 */
static int http_read (struct http *http, char *buf, size_t *lenp)
{
	size_t ret;

	if ((ret = fread(buf, 1, *lenp, http->file)) > 0) {
		*lenp = ret;

		return 0;
	} else if (feof(http->file)) {
		return 1;

	} else {
		log_pwarning("fread");
		return -1;
	}
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
	char *c;

	if (!fgets(http->buf, sizeof(http->buf), http->file)) {
		// EOF?
		if (feof(http->file))
			return 1;

		log_perror("fgets");
		return -1;
	}
	
	// rstrip \r\n
	c = &http->buf[strlen(http->buf)];

	if (*--c != '\n') {
		log_warning("truncated line");
		return -1;
	}

	if (*--c != '\r') {
		log_warning("truncated line");
		return -1;
	}

	*c = '\0';
	*linep = http->buf;

	log_debug("%s", http->buf);

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
		return -1;

	return 0;
}

/* Client request writing */
int http_write_request_start (struct http *http, const char *method, const char *path)
{
	return http_write_line(http, "%s %s %s", method, path, http->version);
}

int http_write_request_start_path (struct http *http, const char *method, const char *fmt, ...)
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

int http_write_request_header (struct http *http, const char *header, const char *value)
{
	return http_write_header(http, header, "%s", value);
}

int http_write_request_headerf (struct http *http, const char *header, const char *fmt, ...)
{
	va_list args;
	int err;

	va_start(args, fmt);
	err = http_write_headerv(http, header, fmt, args);
	va_end(args);

	return err;
}

int http_write_request_end (struct http *http)
{
	return http_write_line(http, "");
}

int http_write_request_body (struct http *http, char *buf, size_t *lenp)
{
	return http_write(http, buf, lenp);
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
		return -1;

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
		return -1;

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

	if ((err = http_read_line(http, &line)))
		return err;
	
	if (!*line) {
		log_debug("end of headers");
		return 1;
	}

	return http_parse_header(line, headerp, valuep);
}

int http_read_body (struct http *http, char *buf, size_t *lenp)
{
	return http_read(http, buf, lenp);
}

void http_destroy (struct http *http)
{
    if (fclose(http->file))
        log_warning("fclose");

    free(http);
}
