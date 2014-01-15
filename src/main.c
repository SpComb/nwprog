#include <getopt.h>
#include <stdio.h>

#include "common/log.h"

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

int apply (const char *arg) {
	log_msg("%s", arg);

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
		err = apply(argv[optind++]);
	}
	
	return err;
}
