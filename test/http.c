#include "common/http.h"
#include "common/http_test.h"
#include "common/log.h"

#include <stdio.h>
#include <string.h>

int test_response (const char *str)
{
    char buf[1024];

	strncpy(buf, str, sizeof(buf));

	// parse
	const char *version, *reason;
	unsigned status;

	if (http_parse_response(buf, &version, &status, &reason)) {
		log_error("[ERROR] '%s'", str);
	}

	log_info("[OK] '%s': version=%s status=%u reason=%s", str, version, status, reason);

	return 0;
}

int test_header (const char *str)
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

	log_set_level(LOG_INFO);
	
	// skip argv0
	argv++;

	// first arg is response line
	err |= test_response(*argv++);

	// next lines are headers
	while ((arg = *argv++)) {
		err |= test_header(arg);
	}

	return err;
}
