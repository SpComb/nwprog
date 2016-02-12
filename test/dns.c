#include "dns.h"
#include "dns/dns.h"

#include "common/log.h"
#include "test.h"

#include <stdio.h>
#include <string.h>

struct pack_name_test {
    const char *pack;

    char buf[512];

    const char *unpack;
} pack_name_tests[] = {
    { "",                   { 0 },                                                      ""          },
    { "localhost",          { 9, 'l', 'o', 'c', 'a', 'l', 'h', 'o', 's', 't', 0 },      "localhost" },
    { "foo.bar",            { 3, 'f', 'o', 'o', 3, 'b', 'a', 'r', 0 },                  "foo.bar"   },

    { ".",                  { 0 },                                                      ""          },
    { "foo",                { 3, 'f', 'o', 'o', 0 },                                    "foo"       },
    { "foo.",               { 3, 'f', 'o', 'o', 0 },                                    "foo"       },
    { "foo..",              { 3, 'f', 'o', 'o', 0 },                                    "foo"       },
    { ".foo",               { 3, 'f', 'o', 'o', 0 },                                    "foo"       },
    { ".foo.",              { 3, 'f', 'o', 'o', 0 },                                    "foo"       },

    { }
};

int test_pack_name (const struct pack_name_test *test) {
    struct dns_packet pkt;
    int err;

    // reset
    pkt.ptr = pkt.buf;
    pkt.end = pkt.buf + sizeof(pkt.buf);

    if (test->pack) {
        if (dns_pack_name(&pkt, test->pack)) {
            log_error("[ERROR] pack %s", test->pack);
            return -1;
        }

        for (const char *c = pkt.buf, *p = test->buf; ; c++, p++) {
            if (*c != *p) {
                log_warning("[FAIL] %s @ %ld: %02x != %02x", test->pack, (p - test->pack), *c, *p);
                return 1;

            } else if (!*p) {
                break;
            }
        }

        pkt.end = pkt.ptr;

    } else {
        size_t size = strlen(test->buf);

        memcpy(pkt.buf, test->buf, size);

        pkt.end = pkt.buf + size;
    }

    if (test->unpack) {
        char name[DNS_NAME];
        pkt.ptr = pkt.buf;

        if (dns_unpack_name(&pkt, name, sizeof(name))) {
            log_error("[ERROR] unpack %s", test->pack);
            return -1;
        }

        if ((err = test_string(test->pack, test->unpack, name)))
            return err;
    }

    log_info("[OK] %s <-> %s", test->pack, test->unpack);

    return 0;
}

int main (int argc, char **argv)
{
    int err = 0;

    log_set_level(LOG_DEBUG);
    
    // skip argv0
    argv++;

    for (struct pack_name_test *test = pack_name_tests; test->pack || test->unpack; test++) {
        err |= test_pack_name(test);
    }

    return err;
}
