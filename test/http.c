#include "common/http.h"
#include "common/http_test.h"
#include "common/log.h"

#include <stdio.h>
#include <string.h>

int test_arg (const char *str)
{
    char buf[1024];

	strncpy(buf, str, sizeof(buf));

	// parse
	const char *header, *value;

	if (http_parse_header(buf, &header, &value)) {
		log_error("[ERROR] '%s'", str);
	}

	log_info("[OK] '%s': header='%s' value='%s'", str, header, value);

	return 0;
}

int main (int argc, char **argv)
{
	const char *arg;
    int err = 0;
	
	// skip argv0
	argv++;

	// from args
	while ((arg = *argv++)) {
		err |= test_arg(arg);
	}

	return err;
}
