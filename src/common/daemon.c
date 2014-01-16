#include "common/daemon.h"

#include "common/log.h"

#include <signal.h>
#include <unistd.h>

int daemon_init ()
{
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		log_perror("signal");
		return -1;
	}

	return 0;
}
