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
    
    /* Apply */
#ifdef WITH_SSL
    struct ssl_main *ssl_main;
#endif
};

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
			"	-h --help          Display this text\n"
			"	-q --quiet         Less output\n"
			"	-v --verbose       More output\n"
			"	-d --debug         Debug output\n"
			"\n"
			"	-G --get=file      GET to file\n"
			"	-P --put=file      PUT from file\n"
            "\n"
			"	-I --iam=username  Send Iam header\n"
			"\n"
			"Examples:\n"
			"\n"
			"	%s -q http://www.ietf.org/rfc/rfc2616.txt\n"
			"	%s -G rfc2616.txt http://www.ietf.org/rfc/rfc2616.txt\n"
			"	%s -P test.txt http://nwprog1.netlab.hut.fi:3000/test.txt\n"
			"\n"
	, argv0, argv0, argv0, argv0);
}

int client (const struct options *options, const char *arg) {
	struct urlbuf urlbuf;
	struct client *client = NULL;
	FILE *get_file = stdout, *put_file = NULL;
	int ret = 0;

	if (urlbuf_parse(&urlbuf, arg)) {
		log_fatal("invalid url: %s", arg);
		ret = 1;
		goto error;
	}

	// handle empty path
	if (!urlbuf.url.path) {
		urlbuf.url.path = "";
	}

	if (options->get && !(get_file = fopen(options->get, "w"))) {
		log_error("fopen %s", options->get);
		log_fatal("failed to open --get file for writing");
		ret = 1;
		goto error;
	}

	if (options->put && !(put_file = fopen(options->put, "r"))) {
		log_error("fopen %s", options->put);
		log_fatal("failed to open --put file for reading");
		ret = 1;
		goto error;
	}

	if (client_create(&client)) {
		log_fatal("failed to initialize client");
		ret = 2;
		goto error;
	}
#ifdef WITH_SSL
    if (client_set_ssl(client, options->ssl_main)) {
        log_fatal("failed to initialize client ssl");
        ret = 2;
        goto error;
    }
#endif
	if (client_open(client, &urlbuf.url)) {
		log_fatal("failed to open url: %s", arg);
		ret = 3;
		goto error;
	}

	if (client_set_response_file(client, get_file)) {
		log_fatal("failed to set client response file");
		ret = 2;
		goto error;
	}

	if (options->iam && client_add_header(client, "Iam", options->iam)) {
		log_fatal("failed to set client Iam header");
		ret = 2;
		goto error;
	}
	
	if (put_file) {
		if ((ret = client_put(client, &urlbuf.url, put_file)) < 0) {
			log_fatal("PUT failed: %s", arg);
			ret = 4;
			goto error;
		}

	} else {
		if ((ret = client_get(client, &urlbuf.url)) < 0) {
			log_fatal("GET failed: %s", arg);
			ret = 4;
			goto error;
		}
	}

    if (ret >= 200 && ret < 300) {
        log_debug("Server returned 2xx response: %d", ret);

    } else if (ret >= 300 && ret < 400) {
        log_info("Server returned 3xx response: %d", ret);

    } else if (ret >= 400 && ret < 500) {
        log_error("Server returned 4xx response: %d", ret);

    } else if (ret >= 500 && ret < 600) {
        log_warning("Server returned 5xx response: %d", ret);

    } else {
        log_warning("Server returned unknown response type: %d", ret);
    }

error:
	if (client)
		client_destroy(client);

	return ret;
}

int main (int argc, char **argv)
{
	int opt, longopt;
	enum log_level log_level = LOG_LEVEL;
	int err = 0;

    struct options options = {
        .get    = NULL,
        .put    = NULL,
        .iam    = getlogin(),
    };

	while ((opt = getopt_long(argc, argv, "hqvdG:P:I:", main_options, &longopt)) >= 0) {
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

			default:
				help(argv[0]);
				return 1;
		}
	}

	// apply
	log_set_level(log_level);
#ifdef WITH_SSL
    if ((err = ssl_main_create(&options.ssl_main))) {
        log_fatal("ssl_main_create");
        return 1;
    }
#endif
	while (optind < argc && !err) {
		err = client(&options, argv[optind++]);
	}
	
	return err;
}
