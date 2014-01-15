#ifndef PARSE_H
#define PARSE_H

/*
 * In-place string parsing.
 */

struct parse {
	/* From state */
	int state;

	/* Via char */
	char c;

	/* To state */
	int next_state;

	/* Store token */
	const char **dest;
};

/*
 * Parse a string in-place, per given parse state machine.
 */
int parse (const struct parse *parsing, char *str, int state);

#endif
