#include "common/parse.h"

#include "common/log.h"

#include <stdlib.h>

/* Lookup parse state for given state/char */
const struct parse * parse_step (const struct parse *parsing, int state, char c)
{
	const struct parse *p;

	for (p = parsing; p->state || p->c || p->next_state || p->dest; p++) {
		if (p->state == state && p->c == c) {
			return p;
		}
		
		// wildcard
		if (p->state == state && p->c == -1) {
			return p;
		}
	}
	
	return NULL;
}

int parse (const struct parse *parsing, char *str, int state)
{
	char *c = str, *token = str;
	const struct parse *p;
	
	do {
		if ((p = parse_step(parsing, state, *c))) {
			if (p->dest) {
				// end current token
				*c++ = '\0';

				log_debug("%d <- %d = %s", p->next_state, state, token);

				*p->dest = token;

			} else {
				// skip char
				log_debug("%d <- %d : %s", p->next_state, state, c);

				c++;
			}
			
			// begin next token
			state = p->next_state;
			
			if (!(p->flags & PARSE_KEEP)) {
				token = c;
			}

		} else {
			// token continues
			c++;
		}
	} while (*c);

	// terminate
	if ((p = parse_step(parsing, state, *c))) {
		log_debug("%d <- %d:%s", p->next_state, state, token);

		state = p->next_state;

		if (p->dest)
			*p->dest = token;
	}

	return state;
}
