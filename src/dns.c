#include "dns/dns.h"
#include "common/event.h"
#include "common/log.h"
#include "common/util.h"

#include <arpa/inet.h>
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
    { "help",        0,     NULL,        'h' },
    { "quiet",        0,     NULL,        'q' },
    { "verbose",    0,    NULL,        'v'    },
    { "debug",        0,    NULL,        'd'    },

    { "resolver",   1,  NULL,       'R' },
    { }
};

struct dns_task {
    const struct options *options;

    const char *arg;
};

void help (const char *argv0) {
    printf(
            "Usage: %s [options] <host> [<host>] [...]\n"
            "\n"
            "   -h --help          Display this text\n"
            "   -q --quiet         Less output\n"
            "   -v --verbose       More output\n"
            "   -d --debug         Debug output\n"
            "\n"
            "   -R --resolver       DNS resolver address\n"
            "\n"
            "Examples:\n"
            "\n"
            "   %s example.com\n"
            "   %s example.com example.net\n"
            "\n"
    , argv0, argv0);
}

void dns (void *ctx)
{
    struct dns_task *task = ctx;
    const struct options *options = task->options;
    const char *arg = task->arg;

    struct dns_resolve *resolve;
    int err;

    log_info("%s", arg);

    // query
    enum dns_type types[] = { DNS_A, DNS_AAAA, DNS_MX, 0 };
    char name[DNS_NAME];

    if (str_copy(name, sizeof(name), arg)) {
        log_warning("name overflow: %s", name);
        return 1;
    }

    for (enum dns_type *type = types; *type; type++) {
        if ((err = dns_resolve(options->dns, &resolve, name, *type))) {
            log_fatal("dns_resolve: %s", arg);
            return 1;
        }

        // response
        enum dns_section section;
        struct dns_record rr;
        union dns_rdata rdata;

        while (!(err = dns_resolve_record(resolve, &section, &rr, &rdata))) {
            char buf[INET6_ADDRSTRLEN];

            if (rr.type == DNS_A) {
                inet_ntop(AF_INET, &rdata.A, buf, sizeof(buf));
            } else if (rr.type == DNS_AAAA) {
                inet_ntop(AF_INET6, &rdata.AAAA, buf, sizeof(buf));
            } else {
                buf[0] = '\0';
            }

            if (section == DNS_AN && rr.type == DNS_CNAME) {
                printf("%s is an alias for %s\n", rr.name, rdata.CNAME);

                // fix further queries
                if (str_copy(name, sizeof(name), rdata.CNAME)) {
                    log_warning("cname overflow: %s", rdata.CNAME);
                    return 1;
            }

            } else if (section == DNS_AN && rr.type == DNS_A) {
                printf("%s has address %s\n", rr.name, buf);
            } else if (section == DNS_AN && rr.type == DNS_AAAA) {
                printf("%s has IPv6 address %s\n", rr.name, buf);
            } else if (section == DNS_AN && rr.type == DNS_MX) {
                printf("%s mail is handled by %u %s\n", rr.name, rdata.MX.preference, rdata.MX.exchange);
            }
        }
    }

    return 0;
}

int main (int argc, char **argv)
{
    int opt;
    enum log_level log_level = LOG_LEVEL;
    int err = 0;

    struct event_main *event_main;
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

    if ((err = event_main_create(&event_main))) {
        log_fatal("event_main_create");
        goto error;
    }

    if ((err = dns_create(event_main, &options.dns, options.resolver))) {
        log_fatal("dns_create: %s", options.resolver);
        goto error;
    }

    while (optind < argc && !err) {
        struct dns_task task = {
            .options    = &options,
            .arg        = argv[optind++],
        };

        // start async task
        if ((err = event_start(event_main, dns, &task))) {
            log_fatal("event_start: %s", task.arg);
        }
    }

    // mainloop
    if ((err = event_main_run(event_main))) {
        log_fatal("event_main");
    }

error:
    if (options.dns)
        dns_destroy(options.dns);

    return err;
}
