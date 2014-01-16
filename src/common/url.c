#include "common/url.h"

#include "common/log.h"
#include "common/parse.h"

#include <stdbool.h>
#include <string.h>
#include <strings.h>

int urlbuf_parse (struct urlbuf *urlbuf, const char *url_string)
{
    bzero(urlbuf, sizeof(*urlbuf));

	if (strlen(url_string) > sizeof(urlbuf->buf)) {
		log_error("url is too long: %d", strlen(url_string));
		return -1;
	}

	strncpy(urlbuf->buf, url_string, sizeof(urlbuf->buf));
	urlbuf->buf[sizeof(urlbuf->buf) - 1] = '\0';

	log_debug("parse: %s", urlbuf->buf);

	if (url_parse(&urlbuf->url, urlbuf->buf)) {
		log_warning("invalid url: %s", url_string);
		return 1;
	}

    return 0;
}

int url_parse (struct url *url, char *buf)
{
	enum state {
		START = 0,
		START_SEP,
		SCHEME,
		SCHEME_SEP,
		HOST,
		PORT,
		PATH,

	};
    struct parse parsing[] = {
        { START,        '/',        START_SEP   },
        { START,        ':',        SCHEME,     PARSE_SKIP					},
        { START,        0,          HOST,       PARSE_STRING,   			.parse_string = &url->host			},

        { START_SEP,    '/',        HOST        },
        { START_SEP,    0,          PATH,       PARSE_STRING,   			.parse_string = &url->path          },

        { SCHEME,       '/',        SCHEME_SEP,	PARSE_STRING,				.parse_string = &url->scheme		},
        { SCHEME,       -1,         PORT,       PARSE_STRING | PARSE_KEEP, 	.parse_string = &url->host			},

        { SCHEME_SEP,   '/',        HOST        },
        
        { HOST,         ':',        PORT,       PARSE_STRING,   			.parse_string = &url->host          },
        { HOST,         '/',        PATH,       PARSE_STRING,   			.parse_string = &url->host          },
        { HOST,         0,          HOST,       PARSE_STRING,   			.parse_string = &url->host          },

        { PORT,         '/',        PATH,       PARSE_STRING,   			.parse_string = &url->port          },
        { PORT,         0,          PORT,       PARSE_STRING,   			.parse_string = &url->port          },

        { PATH,         0,          PATH,       PARSE_STRING,   			.parse_string = &url->path          },

        { }
    }; 
    int state;

    if ((state = parse(parsing, buf, START)) <= 0) {
        return -1;
    }

	return 0;
}

void url_dump (const struct url *url, FILE *f)
{
	if (url->scheme) {
		fprintf(f, "%s:", url->scheme);
	}

	if (url->host) {
		fprintf(f, "//%s", url->host);

	}
		
	if (url->port) {
		fprintf(f, ":%s", url->port);
	}

	if (url->path) {
		fprintf(f, "/%s", url->path);
	}
}
