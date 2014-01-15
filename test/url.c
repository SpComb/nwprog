#include "common/url.h"
#include "common/log.h"

#include <stdio.h>
#include <string.h>

int test_url (const char *str) {
	struct url url = { };
	char buf[1024];
	char *err;
	
	if (strlen(str) < sizeof(buf)) {
		strcpy(buf, str);
	} else {
		log_msg("url is too long: %d", strlen(str));
	}

	if (url_parse(&url, buf, &err)) {
		log_msg("failed to parse url @ %s", err);
		return 1;
	}

	log_msg("%s: scheme=%s, host=%s, port=%s, path=%s", str, url.scheme, url.host, url.port, url.path);

	url_dump(&url, stdout);
	printf("\n");
	return 0;
}

int main (int argc, char **argv) {
	const char *arg;
	
	// skip argv0
	argv++;

	while ((arg = *argv++)) {
		test_url(arg);
	}

	return 0;
}
