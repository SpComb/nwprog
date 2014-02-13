#include "log.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static enum log_level _log_level = LOG_LEVEL;

static const char * log_level_str (enum log_level level) {
	switch (level) {
		case LOG_FATAL:		return "FATAL";
		case LOG_ERROR:		return "ERROR";
		case LOG_WARNING:	return "WARNING";
		case LOG_INFO:		return "INFO";
		case LOG_DEBUG:		return "DEBUG";
		default:			return "UNKNOWN";
	}
}

void _logv (const char *prefix, enum log_level level, int flags, const char *fmt, va_list args)
{
	// supress below configured log level
	if (level > _log_level)
		return;
    
    if (!(flags & LOG_NOPRE))
    	fprintf(stderr, "%-8s %30s: ", log_level_str(level), prefix);

	vfprintf(stderr, fmt, args);
	
	if (flags & LOG_ERRNO)
		fprintf(stderr, ": %s", strerror(errno));

    if (!(flags & LOG_NOLN))
	    fprintf(stderr, "\n");
}

void _log (const char *prefix, enum log_level level, int flags, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    _logv(prefix, level, flags, fmt, args);
    va_end(args);
}

void log_set_level (enum log_level level)
{
	_log_level = level;
}
