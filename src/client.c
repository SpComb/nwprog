#include "client/client.h"
#include "common/log.h"
#include "common/url.h"

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

struct options {
	const char *get;
	const char *put;
    const char *iam;
    enum http_version http_version;
    
    /* Apply */
	struct client *client;

#ifdef WITH_SSL
    struct ssl_main *ssl_main;
#endif
};

enum opts {
    OPT_START       = 255,
    OPT_HTTP_11,
};

static const struct option long_options[] = {
	{ "help",		0, 	NULL,		'h' },
	{ "quiet",		0, 	NULL,		'q' },
	{ "verbose",	0,	NULL,		'v'	},
	{ "debug",		0,	NULL,		'd'	},
	{ "put",		1,	NULL,		'P' },
    { "http-11",    0,  NULL,       OPT_HTTP_11     },
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
			"	-G --get=file      GET to file\n"
			"	-P --put=file      PUT from file\n"
            "\n"
			"	-I --iam=username   Send Iam header\n"
            "	   --http-11        Send HTTP/1.1 requests\n"
			"\n"
			"Examples:\n"
			"\n"
			"	%s -q http://www.ietf.org/rfc/rfc2616.txt\n"
			"	%s -G rfc2616.txt http://www.ietf.org/rfc/rfc2616.txt\n"
			"	%s -P test.txt http://nwprog1.netlab.hut.fi:3000/test.txt\n"
            "\n"
            "Use of HTTP/1.1 persistent connections:\n"
            "\n"
            "   %s --http-11 http://example.com/foo /bar\n"
			"\n"
	, argv0, argv0, argv0, argv0, argv0);
}

struct client_task {
    const struct options *options;

    const char *arg;
};

void client (void *ctx)
{
    struct client_task *task = ctx;
    const struct options *options = task->options;
	struct urlbuf urlbuf;
	FILE *get_file = stdout, *put_file = NULL;
	int ret = 0;

	if (urlbuf_parse(&urlbuf, task->arg)) {
		log_fatal("invalid url: %s", task->arg);
		return 1;
	}

	// handle empty path
	if (!urlbuf.url.path) {
		urlbuf.url.path = "";
	}

    // connect?
    if (urlbuf.url.host && *urlbuf.url.host) {
        if (client_open(options->client, &urlbuf.url)) {
            log_fatal("client_open: %s", task->arg);
            return 1;
        }
    }

	if (options->get) {
        if (!(get_file = fopen(options->get, "w"))) {
            log_error("fopen %s", options->get);
            log_fatal("failed to open --get file for writing");
            return 2;
        }
        
        // set, with close
        if (client_set_response_file(options->client, get_file, true)) {
            log_fatal("failed to set client response file");
            return 2;
        }
    } else {
        // default
        get_file = stdout;

        // set, without close
        if (client_set_response_file(options->client, get_file, false)) {
            log_fatal("failed to set client response file");
            return 2;
        }
    }

	if (options->put && !(put_file = fopen(options->put, "r"))) {
		log_error("fopen %s", options->put);
		log_fatal("failed to open --put file for reading");
		return 2;
	}

	
	if (put_file) {
		if ((ret = client_put(options->client, &urlbuf.url, put_file)) < 0) {
			log_fatal("PUT failed: %s", task->arg);
            return 3;
		}

	} else {
		if ((ret = client_get(options->client, &urlbuf.url)) < 0) {
			log_fatal("GET failed: %s", task->arg);
            return 3;
		}
	}

    if (ret >= 200 && ret < 300) {
        log_debug("Server returned 2xx response: %d", ret);

        return 0;

    } else if (ret >= 300 && ret < 400) {
        log_info("Server returned 3xx response: %d", ret);
        
        return ret;

    } else if (ret >= 400 && ret < 500) {
        log_error("Server returned 4xx response: %d", ret);
        
        return ret;

    } else if (ret >= 500 && ret < 600) {
        log_warning("Server returned 5xx response: %d", ret);

        return ret;

    } else {
        log_warning("Server returned unknown response type: %d", ret);

        return -1;
    }

    return 0;
}

int main (int argc, char **argv)
{
	int opt;
	enum log_level log_level = LOG_LEVEL;
	int err = 0;

    struct options options = {
        .get    = NULL,
        .put    = NULL,
        .iam    = getlogin(),

        .http_version   = HTTP_10,
    };

    struct event_main *event_main;

	while ((opt = getopt_long(argc, argv, "hqvdG:P:I:", long_options, NULL)) >= 0) {
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
				options.get	= optarg;
				break;

			case 'P':
				options.put = optarg;
				break;
            
            case 'I':
                options.iam = optarg;
                break;

            case OPT_HTTP_11:
                options.http_version = HTTP_11;
                break;

			default:
				help(argv[0]);
				return 1;
		}
	}

	// apply
	log_set_level(log_level);

    if ((err = event_main_create(&event_main))) {
        log_fatal("event_main_create");
        return 1;
    }

#ifdef WITH_SSL
    if ((err = ssl_main_create(&options.ssl_main))) {
        log_fatal("ssl_main_create");
        return 1;
    }
#endif

    // setup client
	if (client_create(event_main, &options.client)) {
		log_fatal("failed to initialize client");
		err = 2;
		goto error;
	}
#ifdef WITH_SSL
    if (client_set_ssl(options.client, options.ssl_main)) {
        log_fatal("failed to initialize client ssl");
        err = 2;
        goto error;
    }
#endif

    if (client_set_request_version(options.client, options.http_version)) {
        log_fatal("failed to set client request http version");
        err = 2;
        goto error;
    }

	if (options.iam && client_add_header(options.client, "Iam", options.iam)) {
		log_fatal("failed to set client Iam header");
		err = 2;
		goto error;
	}

	while (optind < argc && !err) {
        struct client_task task = {
            .options    = &options,
            .arg        = argv[optind++],
        };

        if ((err = event_start(event_main, client, &task))) {
            log_fatal("event_start");
            goto error;
        }
	}

    if ((err = event_main_run(event_main))) {
        log_fatal("event_main_run");
        goto error;
    }

error:
    if (options.client)
        client_destroy(options.client);

	return err;
}
