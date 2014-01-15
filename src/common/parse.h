#ifndef PARSE_H
#define PARSE_H

/*
 * In-place string parsing.
 */

enum parse_type {
	PARSE_NONE		= 0,
	PARSE_STRING,
	PARSE_INT,
	PARSE_UINT,

	// skip token for next state
	PARSE_KEEP,
};

struct parse {
	/* From state */
	int state;

	/* For char, or -1 for wildcard */
	char c;

	/* To state */
	int next_state;

	/* Store token */
	enum parse_type type;

	union {
		const char **parse_string;

		int *parse_int;

		unsigned *parse_uint;
	};
};

/*
 * Parse a string in-place, per given parse state machine.
 */
int parse (const struct parse *parsing, char *str, int state);

#endif
