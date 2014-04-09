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

/*
 * Parse a string to an int.
 */
int str_int (const char *str, int *intp);
int str_uint (const char *str, unsigned *uintp);

/*
 * Format to output buf, returning output buf on success, NULL on truncation..
 *
 * XXX: just truncate output instead?
 */
const char * str_fmt (char *buf, size_t len, const char *fmt, ...);

#endif
