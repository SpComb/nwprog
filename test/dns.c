#include "dns.h"
#include "dns/dns.h"

#include "common/log.h"

#include <stdio.h>
#include <string.h>

struct pack_name_test {
    const char *name;

    char buf[512];
} pack_name_tests[] = {
    { "",                   { 0 } },
    { "localhost",          { 9, 'l', 'o', 'c', 'a', 'l', 'h', 'o', 's', 't', 0 } },
    { "foo.bar",            { 3, 'f', 'o', 'o', 3, 'b', 'a', 'r', 0 } },

    { ".",                  { 0 } },
    { "foo",                { 3, 'f', 'o', 'o', 0 } },
    { "foo.",               { 3, 'f', 'o', 'o', 0 } },
    { "foo..",              { 3, 'f', 'o', 'o', 0 } },

    { }
};

int test_pack_name (const struct pack_name_test *test) {
    struct dns_packet pkt;

    // reset
    pkt.ptr = pkt.buf;

    // test
    if (dns_pack_name(&pkt, test->name)) {
        log_error("[ERROR] '%s'", test->name);
        return -1;
    }

    for (const char *c = pkt.buf, *p = test->buf; ; c++, p++) {
        if (*c != *p) {
            log_warning("[FAIL] %s @ %ld: %02x != %02x", test->name, (p - test->name), *c, *p);
            return 1;

        } else if (!*p) {
            break;
        }
    }

    log_info("[OK] %s", test->name);

    return 0;
}

int main (int argc, char **argv)
{
    int err = 0;

	log_set_level(LOG_DEBUG);
	
	// skip argv0
	argv++;

    for (struct pack_name_test *test = pack_name_tests; test->name; test++) {
        err |= test_pack_name(test);
    }

	return err;
}
