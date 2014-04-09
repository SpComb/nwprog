#include "dns.h"
#include "dns/dns.h"

#include "common/log.h"
#include "common/util.h"

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>

struct dns_resolve {
    struct dns *dns;

    // debugging purposes
    const char *name;

    // used for event_wait() on response...
    struct event_task *wait;

    // used for both query and response
    struct dns_packet packet;

    // id for query/response
    uint16_t id;

    // query
    struct dns_header query_header;

    // query sent and registered
    bool query;

    // response received
    bool response;

    // response
    struct dns_header response_header;

    // count number of read records
    int response_questions;
    int response_records;

    TAILQ_ENTRY(dns_resolve) dns_resolves;
};

int dns_resolve_create (struct dns *dns, struct dns_resolve **resolvep)
{
    struct dns_resolve *resolve;

    // create state
    if (!(resolve = calloc(1, sizeof(*resolve)))) {
        log_perror("calloc");
        return -1;
    }

    resolve->dns = dns;

    // start packing out query
    resolve->packet.ptr = resolve->packet.buf;
    resolve->packet.end = resolve->packet.buf + sizeof(resolve->packet.buf);

    // pack initial header
    resolve->query_header = (struct dns_header) {
        .qr         = 0,
        .opcode     = DNS_QUERY,
        .rd         = 1,
        .qdcount    = 0, // placeholder, updated on sending
    };

    if (dns_pack_header(&resolve->packet, &resolve->query_header)) {
        log_warning("query header overflow");
        return 1;
    }

    // ok
    *resolvep = resolve;

    return 0;
}

int dns_query_question (struct dns_resolve *resolve, const char *name, enum dns_type type)
{
    struct dns_question question = {
        .qtype      = type,
        .qclass     = DNS_IN,
    };

    if (str_copy(question.qname, sizeof(question.qname), name)) {
        log_warning("qname overflow: %s", name);
        return 1;
    }

    if (dns_pack_question(&resolve->packet, &question)) {
        log_warning("query overflow: %d", resolve->query_header.qdcount);
        return 1;
    }

    resolve->query_header.qdcount++;

    log_info("QD: %s %s:%s", question.qname,
            dns_class_str(question.qclass),
            dns_type_str(question.qtype)
    );

    return 0;
}

/*
 * Send a finished query.
 */
int dns_resolve_query (struct dns_resolve *resolve)
{
    int err;

    // alloc an id
    resolve->id = resolve->query_header.id = resolve->dns->ids++;

    // dispatch with updated header
    if ((err = dns_query(resolve->dns, &resolve->packet, &resolve->query_header))) {
        log_error("dns_query");
        return err;
    }

    // response mapping
    resolve->query = true;

    TAILQ_INSERT_TAIL(&resolve->dns->resolves, resolve, dns_resolves);

    return 0;
}

int dns_resolve_async (struct dns *dns, struct dns_resolve **resolvep, const char *name, enum dns_type type)
{
    struct dns_resolve *resolve;
    int err;

    log_info("%s %s?", name, dns_type_str(type));

    if ((err = dns_resolve_create(dns, &resolve)))
        return err;

    resolve->name = name;

    if ((err = dns_query_question(resolve, name, type)))
        goto err;

    if ((err = dns_resolve_query(resolve)))
        goto err;

    return 0;

err:
    dns_close(resolve);

    return err;
}

/*
 * Wait for a response to any of our resolves.
 */
int dns_resolve_response (struct dns *dns, struct dns_resolve **resolvep)
{
    // TODO: optimize common case of there only being one dns_resolve pending
    struct dns_resolve *resolve;
    struct dns_packet packet;
    struct dns_header header;
    int err;

    // TODO: timeouts
    if ((err = dns_response(dns, &packet, &header))) {
        log_error("dns_response");
        return -1;
    }

    TAILQ_FOREACH(resolve, &dns->resolves, dns_resolves) {
        if (resolve->id == header.id)
            break;
    }

    if (!resolve) {
        log_warning("unmatched response: %u", header.id);
        return 1;
    }

    // copy in response
    size_t size = packet.end - packet.buf;
    memcpy(resolve->packet.buf, packet.buf, size);
    resolve->packet.end = resolve->packet.buf + size;

    // the header has already been read
    resolve->response_header = header;
    resolve->packet.ptr = resolve->packet.buf + (packet.ptr - packet.buf);

    // mark as responded
    resolve->response = true;

    TAILQ_REMOVE(&dns->resolves, resolve, dns_resolves);

    *resolvep = resolve;

    return 0;
}

/*
 * Synchronize pending resolves, multiplexing tasks across the dns state.
 *
 * The resolve should be dns_resolve_query()'d upon call, and will be dns_resolve_response()'d upon return.
 */
int dns_resolve_sync (struct dns_resolve *resolve)
{
    struct event *event = dns_event(resolve->dns);
    struct dns_resolve *next;
    int err;

    /*
     * This uses very scary event_wait/notify() magic amongst all tasks waiting on this one event, which leads to
     * all kinds of interesting (as in "May you live in interesting times") behaviour.
     *
     * event_wait() is based on the use of the event_switch() stack, i.e. tasks call into eachother in a linear
     * hierarchy. event_main() will event_switch() into a task, which may event_start() another task, or event_notify()
     * into another task, and so on. Once a task calls event_yield(), or event_wait(), it will rewind the task stack,
     * eventually ending up back in event_main().
     *
     * However, this falls apart completely if a set of tasks uses event_notify() to freely switch
     * between eachother, i.e. causing a loop in the task stack. Task A that has notify()'d task B may not itself be
     * notify()'d by B (or C).
     *
     * Happily, though, once task A calls event_notify(B), it will still be sitting within event_notify() at the point
     * where B might want to event_notify(A), and thus as long as we ensure that A->wait is not set when it calls
     * event_notify(), we should be reasonably safe...
     *
     * The other issue, though, is that a task that is yield()'ing on an event has a strict responsibility to notify()
     * any other tasks wait()'ing on that event - if it doesn't, those tasks will all die. Thus:
     *
     *  *   if we recv() a response for a different task that has previously wait()'ing, we will notify() it, and it will
     *      eventually return back, whereupon our response may have been recv()'d by some notify()'d task, or we will
     *      yield() if the notify()'d tasks have all finished, or we will wait() for the next main() iteration if some
     *      notify()'d task has yield()'d.
     *
     *  *   if we recv() a response to our own resolv and we return it, our task might either perform another resolv(),
     *      whereupon we will again yield() on the event, and any tasks that notify()'d us will wait(). If our response
     *      was the final resolv for our task, and it goes off to do something else, then other tasks will be able to
     *      continue if they have notify()'d us. However, if we were event_yield()'ing directly from main(), then we
     *      must be careful to keep any other tasks that have wait()'d on us in some previous event_main() iteration
     *      alive by giving them a "fake" notify() to get them to yield() instead.
     */
    while (!resolve->response) {
        if (!event || !event_pending(event)) {
            // there is no task yielding on the event already, so we are free to go ahead and yield on it.
            if (event) {
                log_debug("%s[%u] recv/yield on event[%p]...", resolve->name, resolve->id, event);
            } else {
                log_debug("%s[%u] recv without event...", resolve->name, resolve->id);
            }

            // recv()/yield() a response
            if ((err = dns_resolve_response(resolve->dns, &next))) {
                log_error("%s[%u] dns_resolve_response", resolve->name, resolve->id);
                return -1;
            }

            // the response that we get may not necessarily be our own
            if (next == resolve) {
                log_debug("%s[%u] immediate response", resolve->name, resolve->id);
                break;
            }

            if (!event) {
                // XXX: just buffer the resolv somewhere... currently dns_resolve_response() dequeues it, though..
                log_fatal("dns_resolve_sync called with multiple pending queries in non-task mode; unable to operate");
                return -1;
            }

            // dispatch and redo
            if (!next->wait) {
                log_debug("%s[%u] response for non-waiting resolv %s[%u], assuming it has notify()'d us", resolve->name, resolve->id, next->name, next->id);
                continue;
            }

            log_debug("%s[%u] dispatching response to %s[%u]", resolve->name, resolve->id, next->name, next->id);
            if ((err = event_notify(event, &next->wait))) {
                log_error("event_notify");
                continue;
            }

        } else {
            // wait for some other task to recv our response...
            log_debug("%s[%u] waiting on event[%p] for response...", resolve->name, resolve->id, event);

            if ((err = event_wait(event, &resolve->wait)) < 0) {
                log_error("event_wait");
                return -1;
            }

            if (err) {
                log_warning("%s[%u] timeout", resolve->name, resolve->id);
                return 1;
            }

            if (resolve->response)
                log_debug("%s[%u] got response from wait()", resolve->name, resolve->id);
            else
                log_debug("%s[%u] got notify()'d without a response, assuming some yield()'ing task is asking us to yield()", resolve->name, resolve->id);
        }
    }

    log_debug("%s[%u] has response", resolve->name, resolve->id);

    // in case we were yielding on an event, and other tasks waited on it, and we got our final response and never yield
    // on it again, we must poke another waiting task at this point in order to keep the queue alive.
    next = TAILQ_FIRST(&resolve->dns->resolves);

    if (!next) {
        log_debug("no resolves left");
    } else if (!next->wait) {
        log_debug("%s[%u] is not waiting, we assume they have notify()'d us previously", next->name, next->id);
    } else {
        // go ahead and notify them...
        log_debug("%s[%u] is poking %s[%u] to keep resolvers alive", resolve->name, resolve->id, next->name, next->id);
        if ((err = event_notify(event, &next->wait))) {
            log_error("event_notify");
        }
    }

    return 0;
}

int dns_resolve (struct dns *dns, struct dns_resolve **resolvep, const char *name, enum dns_type type)
{
    struct dns_resolve *resolve;
    int err;

    log_info("%s %s?", name, dns_type_str(type));

    if ((err = dns_resolve_create(dns, &resolve)))
        return err;

    resolve->name = name;

    if ((err = dns_query_question(resolve, name, type)))
        goto err;

    if ((err = dns_resolve_query(resolve)))
        goto err;

    // schedule across multiple resolves
    if ((err = dns_resolve_sync(resolve))) {
        log_error("dns_resolve_sync");
        goto err;
    }

    // invalidate
    resolve->name = NULL;

    // ok
    *resolvep = resolve;

    return resolve->response_header.rcode;

err:
    free(resolve);

    return err;
}

int dns_resolve_multi (struct dns *dns, struct dns_resolve **resolvep, const char *name, enum dns_type *types)
{
    struct dns_resolve *resolve;
    int err;

    if ((err = dns_resolve_create(dns, &resolve)))
        return err;

    resolve->name = name;

    for (; *types; types++) {
        if ((err = dns_query_question(resolve, name, *types)))
            return err;
    }

    if ((err = dns_resolve_query(resolve)))
        goto err;

    // TODO: schedule multiple queries
    if ((err = dns_response(dns, &resolve->packet, &resolve->response_header))) {
        log_error("dns_response");
        goto err;
    }

    // invalidate
    resolve->name = NULL;

    // ok
    *resolvep = resolve;

    return resolve->response_header.rcode;

err:
    free(resolve);

    return err;
}

int dns_resolve_header (struct dns_resolve *resolve, struct dns_header *header)
{
    *header = resolve->response_header;

    return 0;
}

int dns_resolve_question (struct dns_resolve *resolve, struct dns_question *question)
{
    int err;

    if (resolve->response_questions >= resolve->response_header.qdcount)
        return 1;

    // question
    if ((err = dns_unpack_question(&resolve->packet, question))) {
        log_warning("dns_unpack_question: %d", resolve->response_questions);
        return err;
    }

    resolve->response_questions++;

    log_info("QD: %s %s:%s", question->qname,
            dns_class_str(question->qclass),
            dns_type_str(question->qtype)
    );

    return 0;
}

int dns_resolve_record (struct dns_resolve *resolve, enum dns_section *sectionp, struct dns_record *rr, union dns_rdata *rdata)
{
    enum dns_section section;
    int err;

    // skip questions if needed
    struct dns_question question;

    while (!(err = dns_resolve_question(resolve, &question)))
        ;

    if (err < 0)
        return err;

    // section
    if (resolve->response_records < resolve->response_header.ancount) {
        section = DNS_AN;
    } else if (resolve->response_records < resolve->response_header.ancount + resolve->response_header.nscount) {
        section = DNS_AA;
    } else if (resolve->response_records < resolve->response_header.ancount + resolve->response_header.nscount + resolve->response_header.arcount) {
        section = DNS_AR;
    } else {
        return 1;
    }

    if (sectionp)
        *sectionp = section;

    // record
    if ((err = dns_unpack_record(&resolve->packet, rr))) {
        log_warning("dns_unpack_resource: %d", resolve->response_records);
        return -1;
    }

    resolve->response_records++;

    log_ninfo("%s: %s %s:%s %d ", dns_section_str(section), rr->name,
            dns_class_str(rr->class),
            dns_type_str(rr->type),
            rr->ttl
    );

    // decode
    if (rdata) {
        if ((err = dns_unpack_rdata(&resolve->packet, rr, rdata))) {
            log_warning("dns_unpack_rdata: %d", resolve->response_records);
            return -1;
        }

        log_qinfo("%s", dns_rdata_str(rr, rdata));
    }

    return 0;
}

void dns_close (struct dns_resolve *resolve)
{
    if (resolve->query && !resolve->response) {
        log_warning("%s[%u] abort pending query", resolve->name, resolve->id);
        TAILQ_REMOVE(&resolve->dns->resolves, resolve, dns_resolves);
    }

    free(resolve);
}
