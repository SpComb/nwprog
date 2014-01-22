#include "server/static.h"

#include "common/log.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct server_static {
	/* Embed */
	struct server_handler handler;

	const char *root;
    const char *path;
    int flags;

    char *realpath;
};

struct server_static_mimetype {
    const char *glob;
    const char *content_type;
    const char *glyphicon;

} server_static_mimetypes[] = {
    { "*.html",     "text/html",    "globe"         },
    { "*.txt",      "text/plain",   "align-left"    },
    { "*.css",      "text/css"      },
    { }
};

/*
 * Lookup a `struct server_static_mimetype` for the given (file) path.
 */
int server_static_lookup_mimetype (const struct server_static_mimetype **mimep, struct server_static *s, const char *path)
{
    struct server_static_mimetype *mime;

    for (mime = server_static_mimetypes; mime->glob && mime->content_type; mime++) {
        if (fnmatch(mime->glob, path, 0) == 0) {
            *mimep = mime;
            return 0;
        }
    }

    return 1;
}

int server_static_file_get (struct server_static *s, struct server_client *client, FILE *file, const struct stat *stat, const struct server_static_mimetype *mime)
{
	int err;

	// respond
	if ((err = server_response(client, 200, NULL)))
		return err;

    if (mime && (err = server_response_header(client, "Content-Type", "%s", mime->content_type)))
        return err;

	if ((err = server_response_file(client, stat->st_size, file)))
		return err;

	return 0;
}

int server_static_file_put (struct server_static *s, struct server_client *client, FILE *file, const struct stat *stat, const struct server_static_mimetype *mime)
{
    int err;

    // upload
    if ((err = server_request_file(client, file)))
        return err;

    // done
	if ((err = server_response(client, 201, NULL)))
		return err;

    return 0;
}

/*
 * Print out the directory listing for a single item.
 */
static int server_static_dir_item (struct server_client *client, const char *path, bool dir, const char *glyphicon, const char *title)
{
    return server_response_print(client, "\t\t\t<li%s%s%s>%s%s%s<a href='%s%s'>%s%s</a></li>\n",
            title ? " title='" : "", title ? title : "", title ? "'" : "",
            glyphicon ? "<span class='glyphicon glyphicon-" : "", glyphicon ? glyphicon : "", glyphicon ? "'></span>" : "",
            path, dir ? "/" : "",
            path, dir ? "/" : ""
    );
}

/*
 * Send directory listing, in text/html.
 *
 * XXX: we assume that the given path is XSS-free.
 */
int server_static_dir (struct server_static *s, struct server_client *client, DIR *dir, const char *path)
{
	struct dirent *d;
	int err;

    // ensure dir path ends in /
    if (path[strlen(path) - 1] != '/') {
        return server_response_redirect(client, NULL, "%s/", path);
    }

	if ((err = server_response(client, 200, NULL)))
		return err;

    err |= server_response_header(client, "Content-Type", "text/html");
    err |= server_response_print(client, 
            "<!DOCTYPE html>\n"
            "<html>\n"
            "\t<head>\n"
            "\t\t<title>%s</title>\n"
            "\t\t<link rel='Stylesheet' type='text/css' href='/static/index.css'></link>\n"
            "\t</head>\n"
            "\t<body><div class='container'>\n"
            "\t\t<h1><tt>%s</tt></h1>\n"
            "\t\t<ul class='index'>\n", 
            path, path);

    if (strcmp(path, "/") != 0) {
        err |= server_static_dir_item(client, "..", false, "folder-close", NULL);
    }
    
    // first server_response_print finishes headers
	while ((d = readdir(dir))) {
        const struct server_static_mimetype *mime = NULL;

        if (d->d_name[0] == '.')
            continue;

        bool isdir = (d->d_type == DT_DIR);

        if ((server_static_lookup_mimetype(&mime, s, d->d_name)) < 0) {
            log_warning("server_static_lookup_mimetype: %s %s", path, d->d_name);
        }
        
        const char *glyphicon = mime ? mime->glyphicon : NULL;
        const char *title = mime ? mime->content_type : NULL;
        
        if (!glyphicon) {
            glyphicon = isdir ? "folder-open" : "file";
        }
        
        if ((err = server_static_dir_item(client, d->d_name, isdir, glyphicon, title)))
            break;
	}
    
    err |= server_response_print(client, 
            "\t\t</ul>\n"
            "\t</div></body>\n"
            "</html>\n"
            );

	return err;
}

/*
 * Return the most sensible HTTP status code for the current errno value.
 */
enum http_status server_static_error ()
{
    switch (errno) {
        case EACCES:        return 403;
        case EISDIR:        return 405;
        case ENAMETOOLONG:  return 414;
        case ENOENT:        return 404;
        case ENOTDIR:       return 404;
        default:            return 500;
    }
}

/*
 * Lookup request target file, returning an open fd and stat.
 */
int server_static_lookup (struct server_static *ss, const char *reqpath, int mode, int *fdp, struct stat *stat, const struct server_static_mimetype **mimep)
{
    char path[PATH_MAX], rootpath[PATH_MAX];
	int fd = -1;
    int ret;
    
    // strip off the leading prefix
    if (ss->path[strlen(ss->path) - 1] == '/') {
        reqpath += strlen(ss->path) - 1;
    } else {
        reqpath += strlen(ss->path);
    }
	
    if (*reqpath != '/') {
        log_warning("path without leading /: %s", reqpath);
        return 400;
    }

    if ((ret = snprintf(path, sizeof(path), "%s%s", ss->root, reqpath)) < 0) {
        log_perror("snprintf");
        return -1;
    }

    if (ret >= sizeof(path)) {
        log_warning("path is too long: %d", ret);
        return 414;
    }
	
    log_debug("%s", path);

    // pre-check
	if ((fd = open(path, mode, 0644)) < 0) {
		log_perror("open %s", path);
        ret = server_static_error();
        goto error;
	}

    // verify
    if (!realpath(path, rootpath)) {
        log_perror("realpath");
        ret = server_static_error();
        goto error;
    }

    if (strncmp(ss->realpath, rootpath, strlen(ss->realpath)) != 0) {
        log_warning("path outside of root: %s", path);
        ret = 403;
        goto error;
    }

    // stat for meta
    if (fstat(fd, stat)) {
        log_pwarning("fstatat %s", path);
        ret = server_static_error();
        goto error;
    }

    // figure out mimetype
    if ((ret = server_static_lookup_mimetype(mimep, ss, path)) < 0) {
        log_perror("server_static_lookup_mimetype: %s", path);
        goto error;
    }
    
    if (ret) {
        log_warning("no mimetype: %s", path);
        *mimep = NULL;
    }

    *fdp = fd;
    return 0;

error:
    if (fd >= 0)
        close(fd);

    return ret;
}

/*
 * Request handler.
 */
int server_static_request (struct server_handler *handler, struct server_client *client, const char *method, const char *path)
{
	struct server_static *ss = (struct server_static *) handler;
    const struct server_static_mimetype *mime = NULL;

	int fd = -1;
	struct stat stat;
	int ret = 0;
    int open_mode;

	// see if there are any interesting request headers
	const char *header, *value;

	while (!(ret = server_request_header(client, &header, &value))) {

	}

	if (ret < 0)
		goto error;

    // lookup
    if (strcasecmp(method, "GET") == 0 && (ss->flags & SERVER_STATIC_GET)) {
        open_mode = O_RDONLY;

    } else if (strcasecmp(method, "PUT") == 0 && (ss->flags & SERVER_STATIC_PUT)) {
        open_mode = O_WRONLY | O_CREAT | O_TRUNC;

    } else {
        log_warning("unknown method: %s %s", method, path);
        return 400;
    }

    if ((ret = server_static_lookup(ss, path, open_mode, &fd, &stat, &mime))) {
        return ret;
    }

	log_info("%s %s %s %s", ss->root, method, path, mime ? mime->content_type : "(unknown mimetype)");
	
	// check
	if ((stat.st_mode & S_IFMT) == S_IFREG) {
		FILE *file;
        
        if ((open_mode == O_RDONLY)) {
            if (!(file = fdopen(fd, "r"))) {
                log_pwarning("fdopen");
                ret = -1;
                goto error;
            } else {
                fd = -1;
            }

            ret = server_static_file_get(ss, client, file, &stat, mime);

        } else {
            // upload
            if (!(file = fdopen(fd, "w"))) {
                log_pwarning("fdopen");
                ret = -1;
                goto error;
            } else {
                fd = -1;
            }

            ret = server_static_file_put(ss, client, file, &stat, mime);
        }

		if (fclose(file)) {
			log_pwarning("fclose");
		}
	
	} else if ((stat.st_mode & S_IFMT) == S_IFDIR) {
		DIR *dir;

		if (!(dir = fdopendir(fd))) {
			log_pwarning("fdiropen");
			ret = -1;
			goto error;
		} else {
            fd = -1;
		}
		
		ret = server_static_dir(ss, client, dir, path);

		if (closedir(dir)) {
			log_pwarning("closedir");
		}

	} else {
		log_warning("%s/%s: not a file", ss->root, path);
		ret = 404;
		goto error;
	}
	
error:
    if (fd >= 0)
        close(fd);
        
	return ret;
}

int server_static_create (struct server_static **sp, const char *root, struct server *server, const char *path, int flags)
{
	struct server_static *s;

	if (!(s = calloc(1, sizeof(*s)))) {
		log_perror("calloc");
		return -1;
	}
	
	s->root = root;
    s->path = path;
    s->flags = flags;

    if (!(s->realpath = realpath(root, NULL))) {
        log_perror("realpath");
        goto error;
    }
	
	s->handler.request = server_static_request;

	if (server_add_handler(server, (flags & SERVER_STATIC_PUT) ? "PUT" : "GET", path, &s->handler)) {
        log_error("server_add_handler");
        goto error;
    }

	*sp = s;
	return 0;

error:
    free(s);
    return -1;
}

void server_static_destroy (struct server_static *s)
{
    free(s->realpath);
	free(s);
}
