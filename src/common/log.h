#ifndef LOG_H
#define LOG_H

#define log_msg(fmt, ...) _log_msg(__func__, fmt, ##__VA_ARGS__)
void _log_msg (const char *prefix, const char *fmt, ...);


#endif
