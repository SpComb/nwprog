#include "log.h"

#include <stdio.h>
#include <stdarg.h>

void _log_msg (const char *prefix, const char *fmt, ...)
{
	va_list args;

	fprintf(stderr, "%s: ", prefix);

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	fprintf(stderr, "\n");
}
