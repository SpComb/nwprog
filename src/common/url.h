#ifndef URL_H
#define URL_H

#include <stdio.h>

struct url {
	const char *scheme;
	const char *host;
	const char *port;

	/* URL path *without* leading / */
	const char *path;
    
    /* Any ?... query string */
    const char *query;
};

/* Maximum supported url length */
#define URL_MAX 1024

struct urlbuf {
	char buf[URL_MAX];

	struct url url;
};

/*
 * Parse given url string, using given buffer, into an url struct.
 */
int urlbuf_parse (struct urlbuf *urlbuf, const char *url_string);

/*
 * Parse given url string (in-place) into an url struct.
 *
 * This does *not* NULL out url, so you can supply default values.
 */
int url_parse (struct url *url, char *url_string);

/*
 * Decode a urlencoded key=value pair from the given url, updating the pointer to the next item.
 */
int url_decode (char **queryp, const char **namep, const char **valuep);

/*
 * Write out URL to stream.
 */
void url_dump (const struct url *url, FILE *f);

#endif
