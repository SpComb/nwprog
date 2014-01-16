#include "server/server.h"
#include "common/log.h"
#include "common/url.h"

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

struct options {
	const char *iam;
};

static const struct option main_options[] = {
	{ "help",		0, 	NULL,		'h' },
	{ "quiet",		0, 	NULL,		'q' },
	{ "verbose",	0,	NULL,		'v'	},
	{ "debug",		0,	NULL,		'd'	},

	{ "iam",		1,	NULL,		'I' },
	{ }
};

void help (const char *argv0) {
	printf(
			"Usage: %s [options] <url>\n"
			"\n"
			"	-h --help          Display this text\n"
			"	-q --quiet         Less output\n"
			"	-v --verbose       More output\n"
			"	-d --debug         Debug output\n"
			"\n"
			"	-I --iam=username  Send Iam header\n"
			"\n"
	, argv0);
}

int server (const struct options *options, const char *arg)
{
	struct server *server = NULL;
	struct urlbuf urlbuf;
	int err, ret = 0;

	if (urlbuf_parse(&urlbuf, arg)) {
		log_fatal("invalid server url: %s", arg);
		ret = 1;
		goto error;
	}

	log_info("host=%s port=%s iam=%s arg=%s", urlbuf.url.host, urlbuf.url.port, options->iam, arg);

	if (server_create(&server, urlbuf.url.host, urlbuf.url.port)) {
		log_fatal("server_create %s %s", urlbuf.url.host, urlbuf.url.port);
		ret = 1;
		goto error;
	}

	// XXX: mainloop
	while (true) {
		if ((err = server_run(server)) < 0) {
			log_fatal("server_run");
			goto error;
		}

		if (err) {
			log_warning("server_run");
		}
	}

	return 0;

error:
	if (server)
		server_destroy(server);

	return ret;
}

int main (int argc, char **argv)
{
	int opt, longopt;
	enum log_level log_level = LOG_LEVEL;
	int err = 0;
	struct options options = {
		.iam	= getlogin(),
	};

	while ((opt = getopt_long(argc, argv, "hqvdI:", main_options, &longopt)) >= 0) {
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
			
            case 'I':
                options.iam = optarg;
                break;

			default:
				help(argv[0]);
				return 1;
		}
	}

	// apply
	log_set_level(log_level);

	while (optind < argc && !err) {
		err = server(&options, argv[optind++]);
	}
	
	return err;
}
