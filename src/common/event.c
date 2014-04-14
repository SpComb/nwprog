#include "event.h"

#include "common/log.h"
#include "common/util.h"

#include <pcl.h>

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/queue.h>

#ifdef VALGRIND
#include <valgrind/valgrind.h>
#endif // VALGRIND

struct event_main {
    // currently executing task, maintained by event_switch()
    // NULL when in main() -> event_main()
    struct event_task *task;

    /*
     * The set of existing events, which event_main() will operate.
     *
     * event_main() will exit once there are no more events left, or none of the
     * events have any tasks associated.
     */
    TAILQ_HEAD(event_main_events, event) events;
};

struct event {
    struct event_main *event_main;
    
    // fixed state
    int fd;

    /*
     * The flags will be set to nonzero by event_yield() once some task is pending on this event.
     *
     * The flags will be zero when there is no task pending on this event.
     */
    int flags;

    /*
     * Absolute timeout value for this task, valid when flags & EVENT_TIMEOUT.
     */
    struct timeval timeout;

    /*
     * The task that has yielded on this event.
     * Only one task may be yielding on an event at any time!
     *
     * event_main() will then switch into this task.
     */
    struct event_task *task;

	/*
	 * Delayed event_destroy() while within event_main()
	 */
	bool destroy;

    TAILQ_ENTRY(event) event_main_events;
};

struct event_task {
    struct event_main *event_main;

    // debug info
    const char *name;

    // event_start() execution info
    event_task_func *func;
    void *ctx;

	/*
	 * This task has event_register()'d for the given number of events.
	 */
	int registered;

    /*
     * This task is event_wait()'ing on some given event.
     */
    struct event *wait;

    /*
     * This task was woken up for the given event.
     */
    struct event *event;

    /*
     * Set within the task once func() returns.
     *
     * Used by event_switch() to clean up the task once it has returned.
     */
    bool exit;

    // low-level libpcl state
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

int event_get_max (struct event_main *event_main)
{
    // TODO: switch to something better than select()
    return FD_SETSIZE;
}

int event_create (struct event_main *event_main, struct event **eventp, int fd)
{
    struct event *event;

    // TODO: switch to something better than select()
    if (fd >= event_get_max(event_main)) {
        log_error("given fd is too large: %d > %d", fd, event_get_max(event_main));
        return -1;
    }

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

/*
 * This function is responsible for going further down into the task stack, and maintaining the
 * event_main->task state.
 *
 * No other function may use co_call(), and no task may event_switch() into a task that it has been
 * event_switch()'d into from.
 *
 * This function is also resposible for cleaning up after tasks that have exited, and will set *taskp
 * to NULL if that happens.
 */
static void event_switch (struct event_main *event_main, struct event_task **taskp)
{
    struct event_task *task = *taskp;
    struct event_task *main_task = event_main->task;
    const char *main_name = main_task ? main_task->name : "*";

    log_debug("%s[%p] -> %s[%p]", main_name, main_task, task->name, task);

    event_main->task = task;

    co_call(task->co);

    if (task->exit) {
        log_debug("%s[%p] <- %s[%p]: exit %d", main_name, main_task, task->name, task, task->exit);

        // notify caller as well - we might also be deleting ourself!?
        *taskp = NULL;

        free(task->co_stack);
        free(task);

    } else {
        log_debug("%s[%p] <- %s[%p]", main_name, main_task, task->name, task);
    }

    event_main->task = main_task;
}

/*
 * Wrapper around the task main function to take care of cleanup.
 */
static void event_task (void *ctx)
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

    if (!(task->co = co_create(event_task, task, task->co_stack, EVENT_TASK_SIZE))) {
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

    log_debug("-> %s[%p]", task->name, task);

    event_switch(event_main, &task);
    
    log_debug("<- %s[%p]",
            task ? task->name : "***", task
    );

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

int event_register (struct event *event, int flags, const struct timeval *timeout)
{
    struct event_task *task = event->event_main->task;

    if (!task) {
        log_fatal("%d yielding without task; main() should be in event_main() now...", event->fd);
        return -1;
    }

    if (!flags) {
        log_fatal("Task %s[%p] attempted to yield event %d[%p] without flags", task->name, task, event->fd, event);
        return -1;
    }

    if (event->task) {
        log_fatal("Task %s[%p] attempted to override event %d[%p] task %s[%p]", task->name, task, event->fd, event, event->task->name, event->task);
        return -1;
    }
    
    event->task = task;
    event->flags = flags;

    if (timeout) {
        event->flags |= EVENT_TIMEOUT;

        // set timeout in future
        if (timestamp_from_timeout(&event->timeout, timeout)) {
            log_error("timestamp_from_timeout");
            return -1;
        }
        
    } else if (event->flags & EVENT_TIMEOUT) {
        // set immediate timeout
        if (timestamp_now(&event->timeout)) {
            log_error("timestamp_now");
            return -1;
        }
	}

    log_debug("%s[%p] %d(%s%s%s)", task->name, task, event->fd,
            event->flags & EVENT_READ ? "R" : "",
            event->flags & EVENT_WRITE ? "W" : "",
            event->flags & EVENT_TIMEOUT ? "T" : ""
    );

	// mark
	task->registered++;

    return 0;
}

/*
 * Internal wait-for-event_switch()-from-event_main() mechaism.
 *
 * *eventp should be event that we are expecting to yield on, or NULL to yield from any event.
 */
static int _event_yield (struct event_main *event_main, struct event **eventp)
{
    struct event_task *task = event_main->task;

    if (!task) {
        log_fatal("yielding without task; main() should be in event_main() now...");
        return -1;
    }

	if (!task->registered) {
		log_debug("event_main_yield for %s[%p] without any registered events", task->name, task);
		return 1;
	}

    log_debug("<- %s[%p]", task->name, task);

    co_resume();

    struct event *event = task->event;

    log_debug("-> %s[%p] %d(%s%s%s)", task->name, task, event->fd,
            event->flags & EVENT_READ ? "R" : "",
            event->flags & EVENT_WRITE ? "W" : "",
            event->flags & EVENT_TIMEOUT ? "T" : ""
    );
	
	// XXX: this might underflow in some really weird circumstances
	task->registered--;

	if (*eventp && event != *eventp) {
		log_fatal("%s[%p] unexpected return from yield on %d[%p] to %d[%p",
				task->name, task,
				(*eventp)->fd, (*eventp),
				event->fd, event
		);
		return -1;
	}

	*eventp = event;
	task->event = NULL;

	return 0;
}

int event_main_yield (struct event_main *event_main, struct event **eventp)
{
	struct event_task *task = event_main->task;
	struct event *event = NULL;

	if (_event_yield(event_main, &event)) {
		log_warning("_event_yield");
		return -1;
	}
	
    // read event state, TODO: timeouts
    int flags = event->flags;
    
    // clear yield state
    event->flags = 0;
    event->task = NULL;
	
	// ok
	*eventp = event;

	return 0;
}

int event_yield (struct event *event, int flags, const struct timeval *timeout)
{
	if (event_register(event, flags, timeout))
		return -1;
	
	if (_event_yield(event->event_main, &event))
		return -1;

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

int event_sleep (struct event *event, const struct timeval *timeout)
{
	if (event_register(event, EVENT_TIMEOUT, timeout))
		return -1;

	// yield on event
	if (_event_yield(event->event_main, &event))
		return -1;

    // read event state
    int flags = event->flags;

    // clear yield state
    event->flags = 0;
    event->task = NULL;

    if (flags != EVENT_TIMEOUT) {
		struct event_task *task = event->event_main->task;

        log_warning("Task %s[%p] woke up from sleep with non-TIMEOUT flags: %x", task->name, task, flags);
    }

    return 0;
}

int event_wait (struct event *event, struct event_task **waitp)
{
    struct event_task *task = event->event_main->task;

    if (!task) {
        log_fatal("the main task is attempt to wait on event:%d; main() should be in event_main() now...", event->fd);
        return -1;
    }

    if (task->wait) {
        log_fatal("%s[%p] is already waiting on event:%d", task->name, task, task->wait->fd);
        return -1;
    }

    if (*waitp) {
        log_fatal("%s[%p] set to wait into %p which is already set to %p!", task->name, task, waitp, *waitp);
        return -1;
    }

    *waitp = task;
    task->wait = event;

    log_debug("<- %s[%p]", task->name, task);

    co_resume();

    if (*waitp) {
        log_warning("%s[%p] woke up from wait with still the notify-pointer still set to %p!", task->name, task, *waitp);
    }

    if (task->wait != event) {
        log_warning("%s[%p] woke up from wait with its wait-event changed from %p to %p!", task->name, task, event, task->wait);
    }

    task->wait = NULL;

    log_debug("-> %s[%p]", task->name, task);

    return 0;
}

int event_notify (struct event *event, struct event_task **notifyp)
{
    struct event_task *task = event->event_main->task;
    struct event_task *notify = *notifyp;

    if (task->wait) {
        log_fatal("%s[%p] notifying while waiting on event[%p]!?", task->name, task, task->wait);
        return -1;
    }

    if (!notify) {
        log_fatal("%s[%p] notifying invalid wait", task->name, task);
        return -1;
    }

    log_debug("%s[%p] -> %s[%p]",
            task->name, task,
            notify->name, notify
    );

    // mark notify target as notified
    *notifyp = NULL;

    event_switch(event->event_main, &notify);

    log_debug("%s[%p] <- %s[%p]",
            task->name, task,
            notify ? notify->name : "***", notify
    );

    return 0;
}

void event_destroy (struct event *event)
{
	if (event->task && event->task->registered) {
		log_debug("%d[%p] unregistering from task %s[%p]",
				event->fd, event,
				event->task->name, event->task
		);
		event->task->registered--;

	} else if (event->task) {
		log_fatal("%d[%p] with pending task %s[%p]",
				event->fd, event,
				event->task->name, event->task
		);
	}

	event->task = NULL;

	if (event->event_main->task) {
		log_debug("%d[%p] delaying destroy() from task %s[%p]",
				event->fd, event,
				event->event_main->task->name, event->event_main->task
		);

		event->destroy = true;

	} else {
		log_debug("%d[%p]", event->fd, event);

		TAILQ_REMOVE(&event->event_main->events, event, event_main_events);

		free(event);
	}
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

		// event_destroy -safe loop...
		struct event *event_next;

		for (event = TAILQ_FIRST(&event_main->events); event; event = event_next) {
			event_next = TAILQ_NEXT(event, event_main_events);

			// ...delayed GC
			if (event->destroy) {
				event_destroy(event);
				continue;
			}

            // select's FD_SET only supports fd's under a certain limit (e.g. 1k), larger ones invoke undefined behaviour.
            assert(event->fd < FD_SETSIZE);
				
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

            // convert event_timeout timestamp -> select timeout
            if (timeout_from_timestamp(&select_timeout, &event_timeout)) {
                log_warning("timestamp_timeout");
                return -1;
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
                struct event_task *task = timeout_event->task;

                // notify
                timeout_event->flags = EVENT_TIMEOUT;
				task->event = timeout_event;
                
                // NOTE: this may event_destroy(timeout_event)
                event_switch(event_main, &task);
            }
        } else {
            // event_destroy -safe loop...
            struct event *event_next;

            for (event = TAILQ_FIRST(&event_main->events); event; event = event_next) {
                event_next = TAILQ_NEXT(event, event_main_events);

                int flags = 0;

                if (FD_ISSET(event->fd, &read) && (event->flags & EVENT_READ))
                    flags |= EVENT_READ;
                
                if (FD_ISSET(event->fd, &write) && (event->flags & EVENT_WRITE))
                    flags |= EVENT_WRITE;

				if (!flags) {
					// maybe next time!
				
				} else if (event->task) {
                    struct event_task *task = event->task;

                    event->flags = flags;
                    task->event = event;
                    
                    // this may event_destroy(event)
                    event_switch(event_main, &task);
				} else if (event->destroy) {
					log_debug("ignore destroyed event %d[%p] activation", event->fd, event);
				} else {
					log_fatal("spurious event %d[%p] activation", event->fd, event);
				}
            }
        }
    }
}
