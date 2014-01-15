#include "common/url.h"
#include "common/log.h"

#include <stdio.h>
#include <string.h>

int test_url (const char *str) {
	struct url url = { };
	char buf[1024];
	
	if (strlen(str) < sizeof(buf)) {
		strcpy(buf, str);
	} else {
		log_error("url is too long: %d", strlen(str));
        return 1;
	}

	if (url_parse(&url, buf)) {
		log_error("failed to parse url");
		return 1;
	}

	log_info("%s: scheme=%s, host=%s, port=%s, path=%s", str, url.scheme, url.host, url.port, url.path);

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
