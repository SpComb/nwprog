#include "server/server.h"
#include "server/static.h"

#include "common/daemon.h"
#include "common/event.h"
#include "common/log.h"
#include "common/url.h"

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

struct options {
	bool daemon;
	const char *iam;
	const char *S;
};

static const struct option main_options[] = {
	{ "help",		0, 	NULL,		'h' },
	{ "quiet",		0, 	NULL,		'q' },
	{ "verbose",	0,	NULL,		'v'	},
	{ "debug",		0,	NULL,		'd'	},
	{ "daemon",		0,	NULL,		'D'	},

	{ "iam",		1,	NULL,		'I' },
	{ "static",		1,	NULL,		'S' },
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
			"	-D --daemon			Daemonize\n"
			"\n"
			"	-I --iam=username  	Send Iam header\n"
			"	-S --static=path	Serve static files\n"
			"\n"
	, argv0);
}

int server (struct event_main *event_main, const struct options *options, const char *arg)
{
	struct server *server = NULL;
	struct server_static *server_static = NULL;
	struct urlbuf urlbuf;
	int err, ret = 0;

	if (urlbuf_parse(&urlbuf, arg)) {
		log_fatal("invalid server url: %s", arg);
		ret = 1;
		goto error;
	}

	log_info("%s: host=%s port=%s path=%s iam=%s", arg, urlbuf.url.host, urlbuf.url.port, urlbuf.url.path, options->iam);

	if ((err = server_create(event_main, &server, urlbuf.url.host, urlbuf.url.port))) {
		log_fatal("server_create %s %s", urlbuf.url.host, urlbuf.url.port);
		goto error;
	}

	if (options->S) {
		if ((err = server_static_create(&server_static, options->S))) {
			log_fatal("server_static_create: %s", options->S);
			goto error;
		}

		if ((err = server_static_add(server_static, server, urlbuf.url.path))) {
			log_fatal("server_static_add: %s", "/");
			goto error;
		}
	}

	return 0;

error:
	if (server)
		server_destroy(server);

	if (server_static)
		server_static_destroy(server_static);

	return ret;
}

int main (int argc, char **argv)
{
	int opt, longopt;
	enum log_level log_level = LOG_WARNING;
	int err = 0;
	struct options options = {
		.iam	= getlogin(),
	};
    struct event_main *event_main;

	while ((opt = getopt_long(argc, argv, "hqvdDI:S:", main_options, &longopt)) >= 0) {
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

			case 'D':
				options.daemon = true;
			
            case 'I':
                options.iam = optarg;
                break;
			
			case 'S':
				options.S = optarg;
				break;

			default:
				help(argv[0]);
				return 1;
		}
	}

    // setup
	log_set_level(log_level);

	daemon_init();

    if (event_main_create(&event_main)) {
        log_fatal("event_main_create");
        return 1;
    }

    // apply
	while (optind < argc && !err) {
		if ((err = server(event_main, &options, argv[optind++]))) {
            log_fatal("server");
            return 1;
        }
	}


    // run
	if (options.daemon) {
		daemon_start();
	}

    if (event_main_run(event_main)) {
        log_fatal("event_main_run");
        return 1;
    }

    // done
    return 0;
}
