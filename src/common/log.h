#ifndef LOG_H
#define LOG_H

enum log_level {
	LOG_FATAL,
	LOG_ERROR,
	LOG_WARNING,
	LOG_INFO,
	LOG_DEBUG,
};

enum log_flag {
	LOG_ERRNO	= 0x01,
};

void _log (const char *prefix, enum log_level level, int flags, const char *fmt, ...);

#define log_debug(fmt, ...) 	_log(__func__, LOG_DEBUG,	0,			fmt, ##__VA_ARGS__)
#define log_info(fmt, ...) 		_log(__func__, LOG_INFO,	0,			fmt, ##__VA_ARGS__)
#define log_error(fmt, ...) 	_log(__func__, LOG_ERROR,	0,			fmt, ##__VA_ARGS__)
#define log_warning(fmt, ...) 	_log(__func__, LOG_WARNING,	0,			fmt, ##__VA_ARGS__)
#define log_perror(fmt, ...) 	_log(__func__, LOG_ERROR,	LOG_ERRNO,	fmt, ##__VA_ARGS__)
#define log_pwarning(fmt, ...)	_log(__func__, LOG_ERROR,	LOG_ERRNO,	fmt, ##__VA_ARGS__)

#endif
