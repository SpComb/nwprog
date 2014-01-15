#ifndef URL_H
#define URL_H

#include <stdio.h>

struct url {
	const char *scheme;
	const char *host;
	const char *port;

	/* URL path *without* leading / */
	const char *path;
};

/*
 * Parse given url string (in-place) into an url struct.
 */
int url_parse (struct url *url, char *url_string, char **errp);

/*
 * Write out URL to stream.
 */
void url_dump (const struct url *url, FILE *f);

#endif
