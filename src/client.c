#include "client/client.h"
#include "common/log.h"
#include "common/url.h"

#include <getopt.h>
#include <stdio.h>

static const struct option main_options[] = {
	{ "help",		0, NULL,		'h' },
	{ }
};

void help (const char *argv0) {
	printf(
			"Usage: %s [options] <url>\n"
			"\n"
			"	-h	--help			Display this text\n"
	, argv0);
}

int client (const char *arg) {
	struct urlbuf urlbuf;
	struct client *client = NULL;

	if (urlbuf_parse(&urlbuf, arg)) {
		log_error("invalid url: %s", arg);
		return 1;
	}

	if (client_create(&client)) {
		log_error("failed to initialize client");
		return 2;
	}
	
	if (client_open(client, &urlbuf.url)) {
		log_error("failed to open url");
		return 3;
	}

	if (client_get(client, &urlbuf.url)) {
		log_error("failed to GET url");
		return 4;
	}

	return 0;
}

int main (int argc, char **argv)
{
	int opt, longopt, err;

	while ((opt = getopt_long(argc, argv, "h", main_options, &longopt)) >= 0) {
		switch (opt) {
			case 'h':
				help(argv[0]);
				return 0;

			default:
				help(argv[0]);
				return 1;
		}
	}

	while (optind < argc && !err) {
		err = client(argv[optind++]);
	}
	
	return err;
}
