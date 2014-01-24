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
    struct event_task *main_task = event_main->task;
    const char *main_task_name = main_task ? main_task->name : "*";

    log_debug("%s -> %s", main_task_name, task->name);

    event_main->task = task;
    co_call(task->co);
    event_main->task = main_task;

    if (task->exit) {
        log_debug("%s <- %s: exit %d", main_task_name, task->name, task->exit);

        free(task->co_stack);
        free(task);
    } else {
        log_debug("%s <- %s", main_task_name, task->name);
    }
}

static void _event_main (void *ctx)
{
    struct event_task *task = ctx;

    task->func(task->ctx);

    // exit
    task->exit = 1;
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

    event_switch(event_main, task);

    return 0;

error:
    free(task->co_stack);
    free(task);

    return -1;
}

int event_yield (struct event *event, int flags)
{
    struct event_task *task = event->event_main->task;

    if (event->task) {
        log_fatal("%d overriding %s with %s", event->fd, event->task->name, task->name);
    }
    
    event->task = task;
    event->flags = flags;

    log_debug("<- %s:%d", task->name, event->fd);

    co_resume();

    log_debug("-> %s:%d", task->name, event->fd);
     
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

            if (flags) {
                struct event_task *task = event->task;

                event->flags = 0;
                event->task = NULL;

                event_switch(event_main, task);
            }
        }
    }
}
