#ifndef PARSE_H
#define PARSE_H

/*
 * In-place string parsing.
 */

enum parse_flags {
	// do not \0 out char
	PARSE_KEEP		= 0x01,
};

struct parse {
	/* From state */
	int state;

	/* For char, or -1 for wildcard */
	char c;

	/* To state */
	int next_state;

	/* Store token */
	const char **dest;
	
	/* PARSE_* */
	int flags;
};

/*
 * Parse a string in-place, per given parse state machine.
 */
int parse (const struct parse *parsing, char *str, int state);

#endif
