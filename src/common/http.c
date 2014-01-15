#include "common/http.h"

#include "common/log.h"
#include "common/parse.h"

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

/*
 * Write a line to the HTTP socket.
 */
static int http_vwrite (struct http *http, const char *fmt, va_list args)
{
	if (vfprintf(http->file, fmt, args) < 0)
		return -1;
	
	return 0;
}

static int http_write (struct http *http, const char *fmt, ...)
{
	va_list args;
	int err;

	va_start(args, fmt);
	err = http_vwrite(http, fmt, args);
	va_end(args);
	
	return err;
}

static int http_write_line (struct http *http, const char *fmt, ...)
{
	va_list args;
	int err;

	va_start(args, fmt);
	err = http_vwrite(http, fmt, args);
	va_end(args);
	
	if (err)
		return err;

	if ((err = http_write(http, "\r\n")))
		return err;

	return 0;
}

static int http_write_header (struct http *http, const char *header, const char *fmt, ...)
{
	va_list args;
	int err;
	
	if ((err = http_write(http, "%s: ", header)))
		return err;

	va_start(args, fmt);
	err = http_vwrite(http, fmt, args);
	va_end(args);
	
	if (err)
		return err;

	if ((err = http_write(http, "\r\n")))
		return err;

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
	if (!fgets(http->buf, sizeof(http->buf), http->file)) {
		// EOF?
		if (feof(http->file))
			return 1;

		log_perror("fgets");
		return -1;
	}
	
	if (http->buf[strlen(http->buf) - 1] != '\n') {
		log_warning("truncated line");
		return -1;
	}

	*linep = http->buf;

	return 0;
}

/* Client request writing */
int http_client_request_start (struct http *http, const char *method, const char *path)
{
	return http_write_line(http, "%s %s %s", method, path, http->version);
}

int http_client_request_start_path (struct http *http, const char *method, const char *fmt, ...)
{
	va_list args;
	int err;

	if ((err = http_write(http, "%s ", method)))
		return err;

	va_start(args, fmt);
	err = http_vwrite(http, fmt, args);
	va_end(args);
	
	if (err)
		return err;

	if ((err = http_write(http, " %s\r\n", http->version)))
		return err;

	return 0;
}

int http_client_request_header (struct http *http, const char *header, const char *value)
{
	return http_write_header(http, header, "%s", value);
}

int http_client_request_end (struct http *http)
{
	return http_write_line(http, "");
}

int http_client_response_start (struct http *http, const char **versionp, const char **statusp, const char **reasonp)
{
	char *line;
	int err;

	if ((err = http_read_line(http, &line)))
		return err;
	
	enum state { START, VERSION, STATUS, REASON };
	struct parse parsing[] = {
		{ START, 	' ', 	VERSION,	versionp	},
		{ VERSION,	' ', 	STATUS,		statusp		},
		{ STATUS,	'\0',	REASON,		reasonp		},
		{ }
	};

	// parse
	if ((err = parse(parsing, line, START)) != REASON)
		return -1;

	return 0;
}
