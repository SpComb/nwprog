#include "server/server.h"
#include "server/static.h"

#include "common/daemon.h"
#include "common/event.h"
#include "common/log.h"
#include "common/url.h"

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/resource.h>
#include <unistd.h>

struct options {
	bool daemon;
    unsigned nfiles;
	const char *iam;
	const char *S;
    const char *U;

    /* Processed */
    struct server *server;
    struct server_static *server_static;
    struct server_static *server_upload;
};

static const struct option main_options[] = {
	{ "help",		0, 	NULL,		'h' },
	{ "quiet",		0, 	NULL,		'q' },
	{ "verbose",	0,	NULL,		'v'	},
	{ "debug",		0,	NULL,		'd'	},

	{ "daemon",		0,	NULL,		'D'	},
    { "nfiles",     1,  NULL,       'N' },

	{ "iam",		1,	NULL,		'I' },
	{ "static",		1,	NULL,		'S' },
    { "upload",     1,  NULL,       'U' },
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
            "   -N --nfiles         Limit number of open files\n"
			"\n"
			"	-I --iam=username  	Send Iam header\n"
			"	-S --static=path	Serve static files\n"
            "   -U --upload=path    Accept PUT files\n"
			"\n"
	, argv0);
}

/*
 * Set the process number-of-open-files limit to a suitable value for the given event_main.
 */
int init_nfiles (struct options *options, struct event_main *event_main)
{
    int max = event_get_max(event_main);
    struct rlimit nofile;

    if (options->nfiles) {
        nofile.rlim_cur = nofile.rlim_max = options->nfiles;
    } else {
        if (getrlimit(RLIMIT_NOFILE, &nofile)) {
            log_perror("getrlimit: nofile");
            return -1;
        }
    }

    if (!max || nofile.rlim_cur < max) {
        log_info("using --nfiles limit %d < %d", nofile.rlim_cur, max);
        return 0;
    }

    log_warning("currently set --nfiles rlimit %d is too high, adjusting to %d - 1", nofile.rlim_cur, max);

    // safe limit...
    nofile.rlim_cur = nofile.rlim_max = max - 1;

    if (setrlimit(RLIMIT_NOFILE, &nofile)) {
        log_perror("setrlimit: nofile: %d/%d", nofile.rlim_cur, nofile.rlim_max);
        return -1;
    }

    return 0;
}

int main_listen (struct options *options, const char *arg)
{
	struct urlbuf urlbuf;
	int err;

	if ((err = urlbuf_parse(&urlbuf, arg))) {
		log_fatal("invalid server url: %s", arg);
        return err;
	}

	log_info("%s: host=%s port=%s path=%s iam=%s", arg, urlbuf.url.host, urlbuf.url.port, urlbuf.url.path, options->iam);

	if ((err = server_listen(options->server, urlbuf.url.host, urlbuf.url.port))) {
		log_fatal("server_listen %s %s", urlbuf.url.host, urlbuf.url.port);
        return err;
	}

	return 0;
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

	while ((opt = getopt_long(argc, argv, "hqvdDN:I:S:U:", main_options, &longopt)) >= 0) {
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

            case 'N':
                if (str_uint(optarg, &options.nfiles)) {
                    log_fatal("invalid --nfiles/N: %s", optarg);
                    return 1;
                }
                break;

            case 'I':
                options.iam = optarg;
                break;

			case 'S':
				options.S = optarg;
				break;

            case 'U':
                options.U = optarg;
                break;

			default:
				help(argv[0]);
				return 1;
		}
	}

    // setup
	log_set_level(log_level);

	daemon_init();

    if ((err = event_main_create(&event_main))) {
        log_fatal("event_main_create");
        goto error;
    }

    if ((err = init_nfiles(&options, event_main))) {
        log_fatal("invalid --nfiles setting for event mainloop");
        goto error;
    }

    // apply
	if ((err = server_create(event_main, &options.server))) {
		log_fatal("server_create");
        goto error;
	}

	if (options.U) {
		if ((err = server_static_create(&options.server_upload, options.U, options.server, "upload", SERVER_STATIC_PUT))) {
			log_fatal("server_static_create: %s", options.U);
			goto error;
		}
	}

	if (options.S) {
		if ((err = server_static_create(&options.server_static, options.S, options.server, "", SERVER_STATIC_GET))) {
			log_fatal("server_static_add: %s", "/");
			goto error;
		}
	}

    if (options.iam) {
        if ((err = server_add_header(options.server, "Iam", options.iam))) {
            log_fatal("server_add_header: Iam: %s", options.iam);
            goto error;
        }
    }

	while (optind < argc && !err) {
		if ((err = main_listen(&options, argv[optind++]))) {
            log_fatal("server");
			goto error;
        }
	}

    // run
	if (options.daemon) {
		daemon_start();
	}

    if ((err = event_main_run(event_main))) {
        log_fatal("event_main_run");
        goto error;
    }

error:
	if (options.server)
		server_destroy(options.server);

	if (options.server_static)
		server_static_destroy(options.server_static);
    
    if (err)
        return 1;
    else 
        return 0;
}
