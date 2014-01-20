#ifndef EVENT_H
#define EVENT_H

enum event_flag {
    EVENT_READ  = 0x01,
    EVENT_WRITE = 0x02,
};

/*
 * IO reactor.
 */
struct event_main;
struct event;

typedef void (event_handler)(struct event *event, int flags, void *ctx);

/*
 * Prepare a new event_main for use; initially empty.
 */
int event_main_create (struct event_main **event_mainp);

/*
 * Prepare a new event for use; initially inactive.
 */
int event_create (struct event_main *main, struct event **eventp, int fd, event_handler *func, void *ctx);

/*
 * Set the event active state.
 */
int event_set (struct event *event, int flags);
   
/*
 * Deactivate and release resources.
 */
void event_destroy (struct event *event);

/*
 * Mainloop, runs while there are active events.
 */
int event_main_run (struct event_main *event_main);

#endif
