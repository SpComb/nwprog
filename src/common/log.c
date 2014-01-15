#include "log.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void _log (const char *prefix, enum log_level level, int flags, const char *fmt, ...)
{
	va_list args;

	fprintf(stderr, "%s: ", prefix);

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	
	if (flags & LOG_ERRNO)
		fprintf(stderr, ": %s", strerror(errno));

	fprintf(stderr, "\n");
}
