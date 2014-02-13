#include "dns/dns.h"
#include "common/log.h"

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

struct options {
    const char *resolver;

    struct dns *dns;
};

enum opts {
    OPT_START       = 255,
};

static const struct option long_options[] = {
	{ "help",		0, 	NULL,		'h' },
	{ "quiet",		0, 	NULL,		'q' },
	{ "verbose",	0,	NULL,		'v'	},
	{ "debug",		0,	NULL,		'd'	},

    { "resolver",   1,  NULL,       'R' },
	{ }
};

void help (const char *argv0) {
	printf(
			"Usage: %s [options] ...\n"
			"\n"
			"	-h --help          Display this text\n"
			"	-q --quiet         Less output\n"
			"	-v --verbose       More output\n"
			"	-d --debug         Debug output\n"
            "\n"
            "   -R --resolver       DNS resolver address\n"
			"\n"
			"Examples:\n"
			"\n"
			"	%s\n"
			"\n"
	, argv0, argv0);
}

int dns (const struct options *options, const char *arg) {
    int err;

    log_info("%s", arg);

    if ((err = dns_resolve(options->dns, arg, DNS_A))) {
        log_fatal("dns_resolve: %s", arg);
        return 1;
    }

    return 0;
}

int main (int argc, char **argv)
{
	int opt;
	enum log_level log_level = LOG_LEVEL;
	int err = 0;

    struct options options = {
        .resolver   = "localhost",
    };

	while ((opt = getopt_long(argc, argv, "hqvdR:", long_options, NULL)) >= 0) {
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

            case 'R':
                options.resolver = optarg;
                break;

            default:
				help(argv[0]);
				return 1;
		}
	}

	// apply
	log_set_level(log_level);

    if ((err = dns_create(NULL, &options.dns, options.resolver))) {
        log_fatal("dns_create: %s", options.resolver);
        return 1;
    }

	while (optind < argc && !err) {
		err = dns(&options, argv[optind++]);
	}

	return err;
}
