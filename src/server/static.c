#include "server/static.h"

#include "common/log.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

struct server_static {
	/* Embed */
	struct server_handler handler;

	const char *root;
};

int server_static_file (struct server_static *s, struct server_client *client, FILE *file, const struct stat *stat)
{
	int err;

	// respond
	if ((err = server_response(client, 200, NULL)))
		return err;

	if ((err = server_response_file(client, stat->st_size, file)))
		return err;

	return 0;
}

int server_static_dir (struct server_static *s, struct server_client *client, DIR *dir)
{
	struct dirent *d;
	int err;

	if ((err = server_response(client, 200, NULL)))
		return err;

	while ((d = readdir(dir))) {
		server_response_print(client, "%s\n", d->d_name);
	}

	return 0;
}

int server_static_request (struct server_handler *handler, struct server_client *client, const char *method, const char *path)
{
	struct server_static *s = (struct server_static *) handler;

	int dirfd = -1, filefd = -1;
	int ret = 0;
	struct stat stat;

	// see if there are any interesting request headers
	const char *header, *value;

	while (!(ret = server_request_header(client, &header, &value))) {

	}

	if (ret < 0)
		goto error;
	
	// log
	if (*path == '/') {
		path++;
	}

	log_debug("%s%s", s->root, path);

	// first open the dir
	if ((dirfd = open(s->root, O_RDONLY)) < 0) {
		log_perror("open %s", s->root);
		goto error;
	}
	
	if (*path) {
		// stat for meta
		if (fstatat(dirfd, path, &stat, 0)) {
			log_pwarning("fstatat %s/%s", s->root, path);
			ret = 404;
			goto error;
		}

		// then open the file
		if ((filefd = openat(dirfd, path, O_RDONLY)) < 0) {
			log_pwarning("open %s/%s", s->root, path);
			ret = 403;
			goto error;
		}
	} else {
		// use dir root
		if (fstat(dirfd, &stat)) {
			log_pwarning("fstat %s", s->root);
			ret = 403;
			goto error;
		}
		
		filefd = dirfd;
		dirfd = -1;
	}
	
	log_info("%s %s %s", s->root, method, path);

	// check
	if ((stat.st_mode & S_IFMT) == S_IFREG) {
		FILE *file;

		if (!(file = fdopen(filefd, "r"))) {
			log_pwarning("fdopen");
			ret = -1;
			goto error;
		} else {
			filefd = -1;
		}

		ret = server_static_file(s, client, file, &stat);

		if (fclose(file)) {
			log_pwarning("fclose");
		}
	
	} else if ((stat.st_mode & S_IFMT) == S_IFDIR) {
		DIR *dir;

		if (!(dir = fdopendir(filefd))) {
			log_pwarning("fdiropen");
			ret = -1;
			goto error;
		} else {
			filefd = -1;
		}
		
		ret = server_static_dir(s, client, dir);

		if (closedir(dir)) {
			log_pwarning("closedir");
		}

	} else {
		log_warning("%s/%s: not a file", s->root, path);
		ret = 404;
		goto error;
	}
	
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
