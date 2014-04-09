#include "common/util.h"

#include "common/log.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

const char *strdump (const char *str)
{
	static char buf[STRDUMP_MAX];

	const char *c = str;
	char *outc = buf;
	size_t len = sizeof(buf);
	int ret;

	while (*c) {
		if (*c == '\'') {
			ret = snprintf(outc, len, "\\'");
		} if (isprint(*c)) {
			ret = snprintf(outc, len, "%c", *c);
		} else if (*c == '\n') {
			ret = snprintf(outc, len, "\\n");
		} else if (*c == '\r') {
			ret = snprintf(outc, len, "\\r");
		} else if (*c == '\t') {
			ret = snprintf(outc, len, "\\t");
		} else {
			ret = snprintf(outc, len, "\\0x%02x", *c);
		}
			
		c++;
		
		if (ret < len) {
			outc += ret;
			len -= ret;
		} else {
			// truncated
			break;
		}
	}
	
	return buf;
}

int str_copy (char *buf, size_t size, const char *str)
{
    size_t len = strlen(str);

    if (len >= size)
        return 1;

    memcpy(buf, str, len);

    buf[len] = '\0';

    return 0;
}

int str_int (const char *str, int *intp)
{
    if (sscanf(str, "%d", intp) != 1) {
        log_warning("invalid int: %s", str);
        return 1;
    }

    return 0;
}

int str_uint (const char *str, unsigned *uintp)
{
    if (sscanf(str, "%u", uintp) != 1) {
        log_warning("invalid unsigned int: %s", str);
        return 1;
    }

    return 0;
}

const char * str_fmt (char *buf, size_t len, const char *fmt, ...)
{
    va_list args;
    int ret;

    va_start(args, fmt);
    ret = vsnprintf(buf, len, fmt, args);
    va_end(args);

    if (ret >= len) {
        log_warning("truncated: %s -> %d", fmt, ret);
        return NULL;
    }

    return buf;
}
