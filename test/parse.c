#include "test.h"

#include "common/log.h"
#include "common/parse.h"

#include <limits.h>
#include <stdlib.h>

int test_path ()
{
    int err;
    enum { START, EMPTY, SELF, PARENT, NAME };
    static const struct parse parsing[] = {
        { START,    '/',    EMPTY   },
        { START,    '.',    SELF,   PARSE_KEEP  },
        { START,    -1,     NAME,   PARSE_KEEP  },
        
        { SELF,     '.',    PARENT, PARSE_KEEP  },
        { SELF,     '/',    SELF    },
        { SELF,     -1,     NAME    },

        { PARENT,   '/',    PARENT  },
        { PARENT,   -1,     NAME    },

        { NAME,     '/',    NAME    },

        { }
    };

    struct test_path {
        const char *str;
        const char *out;
        int end; /* state */
    } test_paths[] = {
        { "",       "",         START   },
        { "/",      "",         EMPTY   },
        { ".",      ".",        SELF    },
        { "./",     ".",        SELF    },
        { "..",     "..",       PARENT  },
        { "../",    "..",       PARENT  },

        { "foo",    "foo",      NAME    },
        { "foo/",   "foo",      NAME    },
        { }
    };

    for (struct test_path *test = test_paths; test->str && test->out; test++) {
        char out[PATH_MAX];
        const char *str = test->str;
        int state = START;

        if ((state = tokenize(out, sizeof(out), parsing, &str, state)) < 0) {
            log_error("tokenize %s", test->str);
            return -1;
        }
        
        log_info("%s -> %d", test->str, state);

        if ((err = test_string("out", test->out, out))) {
            return err;
        }

        if (state != test->end) {
            log_warning("[fail] state: %d <= %d", test->end, err);
            return 1;
        }
    }

    return 0;
}

int test_arg (const char *arg)
{
    return 0;
}

int main (int argc, char **argv)
{
	const char *arg;
    int err = 0;

	// skip argv0
	argv++;
    
    if (*argv) {
		log_set_level(LOG_DEBUG);

        // from args
        while ((arg = *argv++)) {
            err |= test_arg(arg);
        }
    } else {
		log_set_level(LOG_INFO);

        err |= test_path();
    }

	return err;
}
