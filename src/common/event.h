#ifndef EVENT_H
#define EVENT_H

#include <sys/time.h>

enum event_flag {
    EVENT_READ      = 0x01,
    EVENT_WRITE     = 0x02,

    EVENT_TIMEOUT   = 0x08,
};

/*
 * Maximum stack size for event_task's.
 *
 * This is allocated by event_start using malloc(), and will never grow.
 * However, perhaps we can rely on Linux's lazy malloc() page allocation..
 */
#define EVENT_TASK_SIZE 65536


/*
 * IO reactor.
 */
struct event_main;
struct event;
struct event_task;

typedef void (event_task_func)(void *ctx);

/*
 * Prepare a new event_main for use; initially empty.
 */
int event_main_create (struct event_main **event_mainp);

/*
 * Return the limit on acceptable fd's for use with event_create.
 * The returned value is the number of acceptable FDs, i.e. fd == max is invalid.
 *
 * Returns 0 on no limit, >0 on limit.
 */
int event_get_max (struct event_main *event_main);

/*
 * Prepare a new event for use; initially inactive.
 */
int event_create (struct event_main *event_main, struct event **eventp, int fd);

/*
 * Boot up the given event task.
 *
 * The task will be cleaned up once the func returns.
 */
int _event_start (struct event_main *event_main, const char *name, event_task_func *func, void *ctx);
#define event_start(event_main, func, ctx) _event_start(event_main, #func, func, ctx)

/*
 * Test if the given event is already pending on (some other task has yielded on it).
 *
 * Returns <0 on error, 0 on clear, 1 on pending.
 */
int event_pending (struct event *event);

/*
 * Register for wakeups on given event, without yielding.
 */
int event_register (struct event *event, int flags, const struct timeval *timeout);

/*
 * Yield execution on registered events.
 *
 * Returns 1 if there are no more registered events.
 *
 * XXX: how to signal timeout?
 */
int event_main_yield (struct event_main *event_main, struct event **eventp);

/*
 * Yield execution on the given event.
 *
 *  flags:          some combination of EVENT_READ|EVENT_WRITE.
 *  timeout:        relative timeout until returning 1 for timeout.
 *
 * Returns 0 on success (event happaned), 1 on timeout (event did not happen), <0 on error.
 */
int event_yield (struct event *event, int flags, const struct timeval *timeout);

/*
 * Pause execution until the next event-loop iteration.
 */
int event_sleep (struct event *event, const struct timeval *timeout);

/*
 * Yield execution on given event, waiting for the task that has event_yield()'d on that event to event_notify() us.
 *
 * TODO: some kind of tracking of dead tasks that are waiting on an event that nobody is yielding on.
 * TODO: support for timeouts
 */
int event_wait (struct event *event, struct event_task **waitp);

/*
 * Transfer execution from a task that has event_yield()'d on the given event to the task that has event_wait()'d on the same event.
 *
 * A task that is notify()'ing another task MAY NOT itself be wait()'ing..
 */
int event_notify (struct event *event, struct event_task **notifyp);

/*
 * Deactivate and release resources.
 *
 * Note: destroying an event from within a task will not actually destroy it, but rather mark it for 
 * safe cleanup within the event_main() loop.
 */
void event_destroy (struct event *event);

/*
 * Mainloop, runs while there are active events.
 */
int event_main_run (struct event_main *event_main);

#endif
