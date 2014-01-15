#include "client/client.h"
#include "common/log.h"
#include "common/url.h"

#include <getopt.h>
#include <stdio.h>

static const struct option main_options[] = {
	{ "help",		0, 	NULL,		'h' },
	{ "quiet",		0, 	NULL,		'q' },
	{ "verbose",	0,	NULL,		'v'	},
	{ "debug",		0,	NULL,		'd'	},
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
	, argv0);
}

int client (const char *arg) {
	struct urlbuf urlbuf;
	struct client *client = NULL;

	if (urlbuf_parse(&urlbuf, arg)) {
		log_fatal("invalid url: %s", arg);
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

	if (client_get(client, &urlbuf.url)) {
		log_fatal("failed to GET url");
		return 4;
	}

	return 0;
}

int main (int argc, char **argv)
{
	int opt, longopt;
	enum log_level log_level = LOG_LEVEL;
	int err = 0;

	while ((opt = getopt_long(argc, argv, "hqvd", main_options, &longopt)) >= 0) {
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

			default:
				help(argv[0]);
				return 1;
		}
	}

	// apply
	log_set_level(log_level);

	while (optind < argc && !err) {
		err = client(argv[optind++]);
	}
	
	return err;
}
