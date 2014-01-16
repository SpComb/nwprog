#include "server/static.h"

#include "common/log.h"

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

struct server_static {
	/* Embed */
	struct server_handler handler;

	const char *root;
};

int server_static_request (struct server_handler *handler, struct server_client *client, const char *method, const char *path)
{
	struct server_static *s = (struct server_static *) handler;

	int dirfd = -1, filefd = -1;
	int ret = 0;
	struct stat stat;
	FILE *file;

	// see if there are any interesting request headers
	const char *header, *value;

	while (!(ret = server_request_header(client, &header, &value))) {

	}

	if (ret < 0)
		goto error;

	// first open the dir
	if ((dirfd = open(s->root, O_RDONLY)) < 0) {
		log_perror("open %s", s->root);
		goto error;
	}

	// then open the file
	if (*path == '/') {
		path++;
	}

	if ((filefd = openat(dirfd, path, O_RDONLY)) < 0) {
		log_pwarning("open %s/%s", s->root, path);
		ret = 404;
		goto error;
	}

	log_info("%s: %s %s", s->root, method, path);
	
	// stat for filetype and size
	if (fstat(filefd, &stat)) {
		log_pwarning("fstat");
		ret = 403;
		goto error;
	}

	// check
	if (stat.st_mode & S_IFMT != S_IFREG) {
		log_warning("%s/%s: not a file", s->root, path);
		ret = 404;
		goto error;
	}
	
	// open stream
	if (!(file = fdopen(filefd, "r"))) {
		log_pwarning("fdopen");
		ret = -1;
		goto error;
	} else {
		filefd = -1;
	}
	
	// respond
	if ((ret = server_response(client, 200, NULL)))
		goto error;

	if ((ret = server_response_file(client, stat.st_size, file)))
		goto error;
	
error:
	if (dirfd > 0)
		close(dirfd);

	if (filefd > 0)
		close(filefd);

	return ret;
}

int server_static_create (struct server_static **sp, const char *root)
{
	struct server_static *s;

	if (!(s = calloc(1, sizeof(*s)))) {
		log_perror("calloc");
		return -1;
	}
	
	s->root = root;
	
	s->handler.request = server_static_request;

	*sp = s;
	return 0;
}

int server_static_add (struct server_static *s, struct server *server, const char *path)
{
	return server_add_handler(server, "GET", path, &s->handler);
}

void server_static_destroy (struct server_static *s)
{
	free(s);
}
