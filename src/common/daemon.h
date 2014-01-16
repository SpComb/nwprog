#ifndef DAEMON_H
#define DAEMON_H

/*
 * Become a daemon.
 */
int daemon_init ();

/*
 * Detach from terminal.
 */
int daemon_start ();

#endif
