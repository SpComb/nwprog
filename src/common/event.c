#include "event.h"

#include "common/log.h"

//#include <libpcl.h>

#include <stdbool.h>
#include <stdlib.h>
#include <sys/queue.h>

struct event_main {
    TAILQ_HEAD(event_main_events, event) events;
};

struct event {
    struct event_main *event_main;

    int fd;
    int flags;

    event_handler *func;
    void *ctx;

    TAILQ_ENTRY(event) event_main_events;
};

int event_main_create (struct event_main **event_mainp)
{
    struct event_main *event_main;

    if (!(event_main = calloc(1, sizeof(*event_main)))) {
        log_perror("calloc");
        return -1;
    }

    TAILQ_INIT(&event_main->events);

    *event_mainp = event_main;
    return 0;
}

int event_create (struct event_main *event_main, struct event **eventp, int fd, event_handler *func, void *ctx)
{
    struct event *event;

    if (!(event = calloc(1, sizeof(*event)))) {
        log_perror("calloc");
        return -1;
    }
    
    event->event_main = event_main;
    event->fd = fd;
    event->func = func;
    event->ctx = ctx;

    TAILQ_INSERT_TAIL(&event_main->events, event, event_main_events);

    *eventp = event;

    return 0;
}

int event_set (struct event *event, int flags)
{
    event->flags = flags;
    
    return 0;
}

void event_destroy (struct event *event)
{
    TAILQ_REMOVE(&event->event_main->events, event, event_main_events);
}

int event_main_run (struct event_main *event_main)
{
    fd_set read, write;

    while (true) {
        int nfds = 0, ret;
        struct event *event;

        FD_ZERO(&read);
        FD_ZERO(&write);

        TAILQ_FOREACH(event, &event_main->events, event_main_events) {
            if (event->flags & EVENT_READ)
                FD_SET(event->fd, &read);
            
            if (event->flags & EVENT_WRITE)
                FD_SET(event->fd, &write);

            if (event->fd >= nfds)
                nfds = event->fd + 1;
        }

        if (!nfds) {
            log_info("nothing to do");
            return 0;
        }
        
        // TODO: timeout
        if ((ret = select(nfds, &read, &write, NULL, NULL)) < 0) {
            log_perror("select");
            return -1; 
        }

        TAILQ_FOREACH(event, &event_main->events, event_main_events) {
            int flags = 0;

            if (FD_ISSET(event->fd, &read))
                flags |= EVENT_READ;
            
            if (FD_ISSET(event->fd, &write))
                flags |= EVENT_WRITE;

            if (flags)
                event->func(event, flags, event->ctx);
        }
    }
}
