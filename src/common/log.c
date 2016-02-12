#include "log.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static enum log_level _log_level = LOG_LEVEL;
static FILE *_log_file = NULL;

static const char * log_level_str (enum log_level level) {
    switch (level) {
        case LOG_FATAL:        return "FATAL";
        case LOG_ERROR:        return "ERROR";
        case LOG_WARNING:    return "WARNING";
        case LOG_INFO:        return "INFO";
        case LOG_DEBUG:        return "DEBUG";
        default:            return "UNKNOWN";
    }
}

void _logv (const char *prefix, enum log_level level, int flags, const char *fmt, va_list args)
{
    FILE *log_file = stderr;

    if (_log_file) 
        log_file = _log_file;

    // supress below configured log level
    if (level > _log_level)
        return;
    
    if (!(flags & LOG_NOPRE))
        fprintf(log_file, "%-8s %30s: ", log_level_str(level), prefix);

    vfprintf(log_file, fmt, args);
    
    if (flags & LOG_ERRNO)
        fprintf(log_file, ": %s", strerror(errno));

    if (!(flags & LOG_NOLN))
        fprintf(log_file, "\n");

    fflush(log_file);
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

void log_set_file (FILE *file)
{
    _log_file = file;
}
