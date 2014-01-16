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
			"	-G	--get=file		GET to file\n"
			"	-P	--put=file		PUT from file\n"
			"\n"
			"Examples:\n"
			"\n"
			"	%s -q http://www.ietf.org/rfc/rfc2616.txt\n"
			"	%s -G rfc2616.txt http://www.ietf.org/rfc/rfc2616.txt\n"
			"	%s -P test.txt http://nwprog1.netlab.hut.fi:3000/test.txt\n"
			"\n"
	, argv0, argv0, argv0, argv0);
}

int client (const char *arg, const char *get, const char *put) {
	struct urlbuf urlbuf;
	struct client *client = NULL;
	FILE *get_file = stdout, *put_file = NULL;

	if (urlbuf_parse(&urlbuf, arg)) {
		log_fatal("invalid url: %s", arg);
		return 1;
	}

	if (get && !(get_file = fopen(get, "w"))) {
		log_error("fopen %s", get);
		log_fatal("failed to open --get file for writing");
		return 1;
	}

	if (put && !(put_file = fopen(put, "r"))) {
		log_error("fopen %s", put);
		log_fatal("failed to open --put file for reading");
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

	if (client_set_response_file(client, get_file)) {
		log_fatal("failed to set client response file");
		return 2;
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
	const char *put = NULL, *get = NULL;

	while ((opt = getopt_long(argc, argv, "hqvdG:P:", main_options, &longopt)) >= 0) {
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
			
			case 'G':
				get	= optarg;
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
		err = client(argv[optind++], get, put);
	}
	
	return err;
}
