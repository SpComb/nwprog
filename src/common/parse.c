#include "common/parse.h"

#include "common/log.h"

#include <stdio.h>
#include <stdlib.h>

/* Lookup parse state for given state/char */
const struct parse * parse_step (const struct parse *parsing, int state, char c)
{
	const struct parse *p;

	for (p = parsing; p->state || p->c || p->next_state; p++) {
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

/* Write out a parsed token */
int parse_store (const struct parse *parse, char *token)
{
	switch (parse->type) {
		case PARSE_STRING:
			*parse->parse_string = token;

			return 0;
		
		case PARSE_INT:
			if (sscanf(token, "%d", parse->parse_int) != 1) {
				log_warning("invalid int token: %s", token);
				return -1;
			}

			return 0;
		
		case PARSE_UINT:
			if (sscanf(token, "%u", parse->parse_int) != 1) {
				log_warning("invalid int token: %s", token);
				return -1;
			}

			return 0;
	
		case PARSE_NONE:
			return 0;

		default:
			// not used
			return 0;
	}
}

int parse (const struct parse *parsing, char *str, int state)
{
	char *c = str, *token = str;
	const struct parse *p;
	int err;
	
	do {
		if ((p = parse_step(parsing, state, *c))) {
			if (p->type && p->type != PARSE_KEEP) {
				// end current token
				*c++ = '\0';

				log_debug("%d <- %d = %s", p->next_state, state, token);
				
				if ((err = parse_store(p, token)))
					return err;

			} else {
				// skip char
				log_debug("%d <- %d : %s", p->next_state, state, c);

				c++;
			}
			
			// begin next token
			state = p->next_state;
			
			if (p->type != PARSE_KEEP) {
				token = c;
			}

		} else {
			// token continues
			c++;
		}
	} while (*c);

	// terminate
	if ((p = parse_step(parsing, state, *c))) {
		log_debug("%d <- %d = %s", p->next_state, state, token);

		state = p->next_state;

		if ((err = parse_store(p, token)))
			return err;
	}

	return state;
}
