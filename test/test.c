#include "test.h"

#include <string.h>

#include "common/log.h"

int test_string (const char *name, const char *expected, const char *value)
{
    if (!expected && !value) {
        log_debug("[ok] %s: NULL <= NULL", name);
        return 0;
    } else if (!expected && value) {
        log_warning("[fail] %s: NULL <= '%s'", name, value);
        return 1;
    } else if (expected && !value) {
        log_warning("[fail] %s: '%s' <= NULL", name, expected);
        return 1;
    } else if (strcmp(expected, value)) {
        log_warning("[fail] %s: '%s' <= '%s'", name, expected, value);
        return 1;
    } else {
        log_debug("[ok] %s: '%s'", name, expected);
        return 0;
    }
}


