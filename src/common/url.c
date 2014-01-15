#include "url.h"
#include "log.h"

#include <strings.h>
#include <stdbool.h>

int url_parse (struct url *url, char *url_string, char **errp)
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
			log_msg("error parsing %d:%s @ %s", state, token, c);
			return err;
		}
		
		if (state != prev_state) {
			// end current token
			*c++ = '\0';
			
			log_msg("%d <- %d:%s", state, prev_state, token);
			
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
		log_msg("unexpected end %d:%s", state, token);
		return 1;
	}

	log_msg("     %d:%s", state, token);

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
