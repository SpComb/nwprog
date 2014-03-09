#include "common/util.h"

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
