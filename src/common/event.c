#include "event.h"

#include "common/log.h"

#include <pcl.h>

#include <stdbool.h>
#include <stdlib.h>
#include <sys/queue.h>

#ifdef VALGRIND
#include <valgrind/valgrind.h>
#endif // VALGRIND

struct event_main {
    struct event_task *task;

    TAILQ_HEAD(event_main_events, event) events;
};

struct event {
    struct event_main *event_main;
    
    int fd;
    int flags;
    struct timeval timeout;

    struct event_task *task;

    TAILQ_ENTRY(event) event_main_events;
};

struct event_task {
    struct event_main *event_main;

    const char *name;
    event_task_func *func;
    void *ctx;

    bool exit;

    coroutine_t co;
    void *co_stack;

#ifdef VALGRIND
    int co_valgrind;
#endif
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

int event_create (struct event_main *event_main, struct event **eventp, int fd)
{
    struct event *event;

    if (!(event = calloc(1, sizeof(*event)))) {
        log_perror("calloc");
        return -1;
    }
    
    event->event_main = event_main;
    event->fd = fd;

    // ok
    TAILQ_INSERT_TAIL(&event_main->events, event, event_main_events);

    *eventp = event;

    return 0;
}

void event_switch (struct event_main *event_main, struct event_task *task)
{
    struct event_task *main = event_main->task;
    const char *name = main ? main->name : "*";

    log_debug("%s[%p] -> %s[%p]", name, main, task->name, task);

    event_main->task = task;

    co_call(task->co);

    if (task->exit) {
        log_debug("%s[%p] <- %s[%p]: exit %d", name, main, task->name, task, task->exit);

        free(task->co_stack);
        free(task);
    } else {
        log_debug("%s[%p] <- %s[%p]", name, main, task->name, task);
    }

    event_main->task = main;
}

static void _event_main (void *ctx)
{
    struct event_task *task = ctx;

    task->func(task->ctx);

    // exit
    task->exit = 1;

    log_debug("%s[%p] exit", task->name, task);

    // XXX: implicit co_resume()?
}

int _event_start (struct event_main *event_main, const char *name, event_task_func *func, void *ctx)
{
    struct event_task *task;

    if (!(task = calloc(1, sizeof(*task)))) {
        log_perror("calloc");
        return -1;
    }

    task->name = name;

    if (!(task->co_stack = malloc(EVENT_TASK_SIZE))) {
        log_perror("malloc co_stack");
        goto error;
    }

    task->func = func;
    task->ctx = ctx;

    if (!(task->co = co_create(_event_main, task, task->co_stack, EVENT_TASK_SIZE))) {
        log_perror("co_create");
        goto error;
    }

#ifdef VALGRIND
    task->co_valgrind = VALGRIND_STACK_REGISTER(task->co_stack, task->co_stack + EVENT_TASK_SIZE);
    log_info("VALGRIND_STACK_REGISTER(%p, %p) = %d",
            task->co_stack, 
            task->co_stack + EVENT_TASK_SIZE,
            task->co_valgrind
    );
#endif

    log_debug("%s[%p]", task->name, task);

    event_switch(event_main, task);

    return 0;

error:
    free(task->co_stack);
    free(task);

    return -1;
}

int event_pending (struct event *event)
{
    if (event->task)
        return 1;

    return 0;
}

int event_yield (struct event *event, int flags, const struct timeval *timeout)
{
    struct event_task *task = event->event_main->task;

    if (!task) {
        log_fatal("%d yielding without task; main() should be in event_main() now...", event->fd);
        return -1;
    }

    if (event->task) {
        log_fatal("%d overriding %s with %s", event->fd, event->task->name, task->name);
        return -1;
    }
    
    event->task = task;
    event->flags = flags;

    if (timeout) {
        event->flags |= EVENT_TIMEOUT;

        // set timeout in future
        if (gettimeofday(&event->timeout, NULL)) {
            log_perror("gettimeofday");
            return -1;
        }
        
        event->timeout.tv_sec += timeout->tv_sec;
        event->timeout.tv_usec += timeout->tv_usec;
    }

    log_debug("<- %s[%p] %d(%s%s%s)", task->name, task, event->fd,
            event->flags & EVENT_READ ? "R" : "",
            event->flags & EVENT_WRITE ? "W" : "",
            event->flags & EVENT_TIMEOUT ? "T" : ""
    );

    co_resume();

    log_debug("-> %s[%p] %d(%s%s%s)", task->name, task, event->fd,
            event->flags & EVENT_READ ? "R" : "",
            event->flags & EVENT_WRITE ? "W" : "",
            event->flags & EVENT_TIMEOUT ? "T" : ""
    );
    
    // read event state
    flags = event->flags;
    
    // clear yield state
    event->flags = 0;
    event->task = NULL;

    if (flags & EVENT_TIMEOUT)
        return 1;
    else 
        return 0;
}

int event_wait (struct event *event, struct event_task **taskp)
{
    struct event_task *task = event->event_main->task;

    if (!task) {
        log_fatal("%d yielding without task; main() should be in event_main() now...", event->fd);
        return -1;
    }

    *taskp = task;

    log_debug("<- %s[%p]", task->name, task);

    co_resume();

    log_debug("-> %s[%p]", task->name, task);

    return 0;
}

int event_notify (struct event *event, struct event_task **notifyp)
{
    struct event_task *task = event->event_main->task;
    struct event_task *notify = *notifyp;

    if (!notify) {
        log_fatal("[%p] notifying invalid wait", task);
        return -1;
    }

    log_debug("%s[%p] -> %s[%p]",
            task->name, task,
            notify->name, notify
    );

    // mark notify target as notified
    *notifyp = NULL;

    // expecting co_resume() from event_yield()... to return here..
    event_switch(event->event_main, notify);

    log_debug("%s[%p] <- %s[%p]",
            task->name, task,
            notify->name, notify
    );

    return 0;
}

void event_destroy (struct event *event)
{
    TAILQ_REMOVE(&event->event_main->events, event, event_main_events);

    free(event);
}

int event_main_run (struct event_main *event_main)
{
    fd_set read, write;

    while (true) {
        int nfds = 0, ret;
        struct event *event;
        struct timeval event_timeout = { 0, 0 };
        struct event *timeout_event = NULL;

        FD_ZERO(&read);
        FD_ZERO(&write);

        TAILQ_FOREACH(event, &event_main->events, event_main_events) {
            if (event->flags & EVENT_READ)
                FD_SET(event->fd, &read);
            
            if (event->flags & EVENT_WRITE)
                FD_SET(event->fd, &write);

            if (event->flags & EVENT_TIMEOUT) {
                if (!event_timeout.tv_sec || event->timeout.tv_sec < event_timeout.tv_sec || (   
                        event->timeout.tv_sec == event_timeout.tv_sec 
                    &&  event->timeout.tv_usec < event_timeout.tv_usec
                )) {
                    event_timeout = event->timeout;
                    timeout_event = event;
                }
            }

            // mark as active event
            if (event->flags && event->fd >= nfds)
                nfds = event->fd + 1;
        }

        if (!nfds) {
            log_info("exit");
            return 0;
        }

        // select, with timeout?
        if (timeout_event) {
            struct timeval select_timeout;

            if (gettimeofday(&select_timeout, NULL)) {
                log_perror("gettimeofday");
                return -1;
            }
            
            if (event_timeout.tv_sec >= select_timeout.tv_sec && event_timeout.tv_usec >= select_timeout.tv_usec) {
                select_timeout.tv_sec = event_timeout.tv_sec - select_timeout.tv_sec;
                select_timeout.tv_usec = event_timeout.tv_usec - select_timeout.tv_usec;

            } else if (event_timeout.tv_sec > select_timeout.tv_sec && event_timeout.tv_usec < select_timeout.tv_usec) {
                select_timeout.tv_sec = event_timeout.tv_sec - select_timeout.tv_sec - 1;
                select_timeout.tv_usec = 1000000 + event_timeout.tv_usec - select_timeout.tv_usec;

            } else {
                log_warning("timeout in future: %ld:%ld", event_timeout.tv_sec, event_timeout.tv_usec);
                select_timeout.tv_sec = 0;
                select_timeout.tv_usec = 0;
            }

            log_debug("select: %d timeout=%ld:%ld", nfds, select_timeout.tv_sec, select_timeout.tv_usec);
            
            ret = select(nfds, &read, &write, NULL, &select_timeout);

        } else {
            log_debug("select: %d", nfds);

            ret = select(nfds, &read, &write, NULL, NULL);
        }
        
        if (ret < 0) {
            log_perror("select");
            return -1; 
        }
        
        if (!ret) {
            // timed out
            if (!timeout_event) {
                log_error("select timeout without event?!");
            } else {
                // notify
                timeout_event->flags = EVENT_TIMEOUT;
                
                // NOTE: this may event_destroy(event)
                event_switch(event_main, timeout_event->task);
            }
        } else {
            // event_destroy -safe loop...
            struct event *event_next;

            for (event = TAILQ_FIRST(&event_main->events); event; event = event_next) {
                event_next = TAILQ_NEXT(event, event_main_events);

                int flags = 0;

                if (FD_ISSET(event->fd, &read))
                    flags |= EVENT_READ;
                
                if (FD_ISSET(event->fd, &write))
                    flags |= EVENT_WRITE;

                if (flags) {
                    struct event_task *task = event->task;

                    event->flags = flags;
                    
                    // this may event_destroy(event)
                    event_switch(event_main, task);
                }
            }
        }
    }
}
