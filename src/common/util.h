#ifndef UTIL_H
#define UTIL_H

/* Maximum length of output */
#define STRDUMP_MAX 512

/*
 * Return a (static) pointer to a printf-safe format of str.
 */
const char *strdump (const char *str);

#endif
