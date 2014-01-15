#include "client/client.h"
#include "common/log.h"
#include "common/url.h"

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>

static const struct option main_options[] = {
	{ "help",		0, 	NULL,		'h' },
	{ "quiet",		0, 	NULL,		'q' },
	{ "verbose",	0,	NULL,		'v'	},
	{ "debug",		0,	NULL,		'd'	},
	{ "put",		1,	NULL,		'P' },
	{ }
};

void help (const char *argv0) {
	printf(
			"Usage: %s [options] <url>\n"
			"\n"
			"	-h	--help			Display this text\n"
			"	-q	--quiet			Less output\n"
			"	-v	--verbose		More output\n"
			"	-d	--debug			Debug output\n"
			"\n"
			"   -P	--put=file		PUT from file\n"
	, argv0);
}

int client (const char *arg, const char *put) {
	struct urlbuf urlbuf;
	struct client *client = NULL;
	FILE *put_file = NULL;

	if (urlbuf_parse(&urlbuf, arg)) {
		log_fatal("invalid url: %s", arg);
		return 1;
	}

	if (put && !(put_file = fopen(put, "r"))) {
		log_error("fopen");
		log_fatal("failed to open --put file");
		return 1;
	}

	if (client_create(&client)) {
		log_fatal("failed to initialize client");
		return 2;
	}
	
	if (client_open(client, &urlbuf.url)) {
		log_fatal("failed to open url");
		return 3;
	}
	
	if (put_file) {
		if (client_put(client, &urlbuf.url, put_file)) {
			log_fatal("PUT failed: %s", arg);
			return 4;
		}

	} else {
		if (client_get(client, &urlbuf.url)) {
			log_fatal("GET failed: %s", arg);
			return 4;
		}
	}

	return 0;
}

int main (int argc, char **argv)
{
	int opt, longopt;
	enum log_level log_level = LOG_LEVEL;
	int err = 0;
	const char *put = NULL;

	while ((opt = getopt_long(argc, argv, "hqvdP:", main_options, &longopt)) >= 0) {
		switch (opt) {
			case 'h':
				help(argv[0]);
				return 0;
			
			case 'q':
				log_level = LOG_ERROR;
				break;

			case 'v':
				log_level = LOG_INFO;
				break;

			case 'd':
				log_level = LOG_DEBUG;
				break;

			case 'P':
				put = optarg;
				break;

			default:
				help(argv[0]);
				return 1;
		}
	}

	// apply
	log_set_level(log_level);

	while (optind < argc && !err) {
		err = client(argv[optind++], put);
	}
	
	return err;
}
