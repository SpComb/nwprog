#include "test.h"

#include "common/url.h"
#include "common/log.h"

#include <stdio.h>

struct test_url {
    const char *str;
    const struct url url;
} tests[] = {
    { "",                   { .host = "" } },
    { "foo",                { .host	= "foo" } },
    { "/foo",               { .path = "foo" } },
    { "//foo",              { .host = "foo" } },
    
	{ "host:port",                	{ .host	= "host", .port = "port" } },

    { "//host/path",                { .host = "host", .path = "path" } },
    { "//host:port/path",           { .host = "host", .port = "port", .path = "path" } },

    { "scheme://host",              { .scheme = "scheme", .host = "host" } },
    { "scheme://host:port",         { .scheme = "scheme", .host = "host", .port = "port" } },
    { "scheme://host:port/path",    { .scheme = "scheme", .host = "host", .port = "port", .path = "path" } },
    { "scheme://host/path",         { .scheme = "scheme", .host = "host", .path = "path" } },
    
    { "scheme:///path",             { .scheme = "scheme", .host = "", .path = "path" } },

    { "/path",                      { .path = "path" } },
    { "/path/",                     { .path = "path/" } },
    { "/path?query",                { .path = "path", .query = "query" } },

    { "[fe80::foo]:80",             { .host = "fe80::foo", .port = "80" } },

    { }
};

const char *error_tests[] = {
    // XXX: no more invalid parses?
    NULL
};

int test_url (struct test_url *test)
{
    struct urlbuf urlbuf;

    if (urlbuf_parse(&urlbuf, test->str)) { 
		log_error("failed to parse url");
		return -1;
	}

	log_debug("%s: scheme=%s, host=%s, port=%s, path=%s", test->str, urlbuf.url.scheme, urlbuf.url.host, urlbuf.url.port, urlbuf.url.path);
    
    int err = 0;

    err |= test_string("scheme", test->url.scheme, urlbuf.url.scheme);
    err |= test_string("host", test->url.host, urlbuf.url.host);
    err |= test_string("port", test->url.port, urlbuf.url.port);
    err |= test_string("path", test->url.path, urlbuf.url.path);

    if (err) {
        log_warning("[FAIL] %s", test->str);
    } else {
        log_info("[OK] %s", test->str);
    }

    return err;
}

int test_url_error (const char *str)
{
    struct urlbuf urlbuf;
    int err;

    err = urlbuf_parse(&urlbuf, str);

    if (err) {
        log_info("[OK] %s", str);
        return 0;
    } else {
        log_warning("[FAIL] %s", str);
		return 1;
	}
}

int test_arg (const char *str)
{
    struct urlbuf urlbuf;

    if (urlbuf_parse(&urlbuf, str)) { 
		log_error("failed to parse url");
		return 1;
	}

	log_info("%s: scheme=%s, host=%s, port=%s, path=%s", str, urlbuf.url.scheme, urlbuf.url.host, urlbuf.url.port, urlbuf.url.path);

	url_dump(&urlbuf.url, stdout);
	printf("\n");

	return 0;
}

int main (int argc, char **argv)
{
	const char *arg;
    int err;

	
	// skip argv0
	argv++;
    
    if (*argv) {
		log_set_level(LOG_DEBUG);

        // from args
        while ((arg = *argv++)) {
            err |= test_arg(arg);
        }
    } else {
		log_set_level(LOG_INFO);

        for (struct test_url *test = tests; test->str; test++) {
            err |= test_url(test);
        }

        for (const char **str = error_tests; *str; str++) {
            err |= test_url_error(*str);
        }
    }

	return err;
}
