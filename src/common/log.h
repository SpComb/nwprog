#ifndef LOG_H
#define LOG_H

#include <stdarg.h>
#include <stdio.h>

enum log_level {
    LOG_FATAL,
    LOG_ERROR,
    LOG_WARNING,
    LOG_INFO,
    LOG_DEBUG,
};

#define LOG_LEVEL LOG_WARNING

enum log_flag {
    LOG_ERRNO    = 0x01,     // suffix with errno
    LOG_NOLN    = 0x02,     // omit newline for continuation
    LOG_NOPRE   = 0x04,     // omit prefix
};

void _logv (const char *prefix, enum log_level level, int flags, const char *fmt, va_list args);
void _log (const char *prefix, enum log_level level, int flags, const char *fmt, ...)
    __attribute((format (printf, 4, 5)));

#define log_debug(fmt, ...)     _log(__func__, LOG_DEBUG,    0,            fmt, ##__VA_ARGS__)
#define log_pdebug(fmt, ...)     _log(__func__, LOG_DEBUG,    LOG_ERRNO,    fmt, ##__VA_ARGS__)
#define log_ndebug(fmt, ...)    _log(__func__, LOG_DEBUG,    LOG_NOLN,   fmt, ##__VA_ARGS__)
#define log_qdebug(fmt, ...)    _log(__func__, LOG_DEBUG,    LOG_NOPRE,  fmt, ##__VA_ARGS__)
#define log_info(fmt, ...)         _log(__func__, LOG_INFO,    0,            fmt, ##__VA_ARGS__)
#define log_ninfo(fmt, ...)     _log(__func__, LOG_INFO,    LOG_NOLN,   fmt, ##__VA_ARGS__)
#define logv_qinfo(fmt, args)   _logv(__func__, LOG_INFO,    LOG_NOPRE,  fmt, args)
#define log_qinfo(fmt, ...)     _log(__func__, LOG_INFO,    LOG_NOPRE,  fmt, ##__VA_ARGS__)
#define log_warning(fmt, ...)     _log(__func__, LOG_WARNING,    0,            fmt, ##__VA_ARGS__)
#define log_pwarning(fmt, ...)    _log(__func__, LOG_WARNING,    LOG_ERRNO,    fmt, ##__VA_ARGS__)
#define log_error(fmt, ...)     _log(__func__, LOG_ERROR,    0,            fmt, ##__VA_ARGS__)
#define log_perror(fmt, ...)     _log(__func__, LOG_ERROR,    LOG_ERRNO,    fmt, ##__VA_ARGS__)
#define log_fatal(fmt, ...)     _log(__func__, LOG_FATAL,    0,            fmt, ##__VA_ARGS__)
#define log_pfatal(fmt, ...)     _log(__func__, LOG_FATAL,    LOG_ERRNO,    fmt, ##__VA_ARGS__)

/*
 * Set the maximum log level.
 */
void log_set_level (enum log_level level);

/*
 * Redirect logging to given file.
 */
void log_set_file (FILE *file);

#endif
