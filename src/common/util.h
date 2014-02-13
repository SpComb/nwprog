#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

/* Maximum length of output */
#define STRDUMP_MAX 512

/*
 * Return a (static) pointer to a printf-safe format of str.
 */
const char *strdump (const char *str);

/*
 * Copy a NUL-terminated string to a fixed-size buffer.
 */
int str_copy (char *buf, size_t size, const char *str);

#endif
