#ifndef HTTP_TEST_H
#define HTTP_TEST_H

#include "common/http.h"

/*
 * Parse header from line.
 *
 * For a folded header (continuing the previous header), *headerp is left as-is.
 */
int http_parse_header (char *line, const char **headerp, const char **valuep);

#endif
