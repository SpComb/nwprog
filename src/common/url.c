#include "url.h"
#include "log.h"

#include <stdbool.h>
#include <string.h>
#include <strings.h>

int urlbuf_parse (struct urlbuf *urlbuf, const char *url_string)
{
	if (strlen(url_string) > sizeof(urlbuf->buf)) {
		log_error("url is too long: %d", strlen(url_string));
		return 1;
	}

	strncpy(urlbuf->buf, url_string, sizeof(urlbuf->buf));
	urlbuf->buf[sizeof(urlbuf->buf) - 1] = '\0';

	log_debug("parse: %s", urlbuf->buf);

	if (url_parse(&urlbuf->url, urlbuf->buf)) {
		log_error("invalid url: %s", url_string);
		return 1;
	}

    return 0;
}

int url_parse (struct url *url, char *url_string)
{
	char *token = url_string, *c = url_string;
	enum {
		START,
		START_SEP,
		SCHEME,
		SCHEME_SEP,
		HOST,
		PORT,
		PATH,
	} state = START, prev_state = START;
	int err = 0;
	
	while (*c) {
		prev_state = state;

		switch (*c) {
			case ':':
				if (state == START) {
					state = SCHEME;
					url->scheme = token;

				} else if (state == HOST) {
					state = PORT;
					url->host = token;

				} else if (state == PATH) {

				} else {
					err = 1;
				}

				break;
			
			case '/':
				if (state == START) {
					state = START_SEP;

				} else if (state == START_SEP) {
					state = HOST;
					url->host = token;

				} else if (state == SCHEME) {
					state = SCHEME_SEP;

				} else if (state == SCHEME_SEP) {
					state = HOST;
				
				} else if (state == HOST) {
					url->host = token;
					state = PATH;

				} else if (state == PORT) {
					url->port = token;
					state = PATH;

				} else if (state == PATH) {

				} else {
					err = 1;
				}

				break;

			default:
				if (state == SCHEME) {
					state = PATH;
					url->path = token;
					
				} else {

				}
		}

		if (err) {
			log_warning("error parsing %d:%s @ %s", state, token, c);
			return err;
		}
		
		if (state != prev_state) {
			// end current token
			*c++ = '\0';
			
			log_debug("%d <- %d:%s", state, prev_state, token);
			
			// begin next token
			token = c;
		} else {
			c++;
		}

	} while (*c);

	// terminus
	if (state == START) {
		url->scheme = token;
	
	} else if (state == START_SEP) {
		url->path = token;
	
	} else if (state == HOST) {
		url->host = token;

	} else if (state == PATH) {
		url->path = token;

	} else {
		log_warning("unexpected end %d:%s", state, token);
		return 1;
	}

	log_debug("     %d:%s", state, token);

	return 0;
}

void url_dump (const struct url *url, FILE *f)
{
	if (url->scheme) {
		fprintf(f, "%s:", url->scheme);
	}

	if (url->host) {
		fprintf(f, "//%s", url->host);

		if (url->port) {
			fprintf(f, ":%s", url->port);
		}
	}

	if (url->path) {
		fprintf(f, "/%s", url->path);
	}
}
