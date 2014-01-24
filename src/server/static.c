#include "server/static.h"

#include "common/log.h"
#include "common/parse.h"

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
    
    // first server_response_print finishes headers
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
    
    // directory items
	while ((d = readdir(dir))) {
        const struct server_static_mimetype *mime = NULL;
        
        // ignore hidden files
        if (d->d_name[0] == '.')
            continue;

        bool isdir = (d->d_type == DT_DIR);
        
        // eyecandy
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
        case ELOOP:         return 404; // TODO: something better
        case ENOENT:        return 404;
        case ENOTDIR:       return 404;
        default:            return 500;
    }
}

/*
 * Lookup request target file, returning an open fd and stat.
 *
 * `mode` is the set of open flags to use, usually O_RDONLY, but can be used to create/write files for upload.
 */
int server_static_lookup (struct server_static *ss, const char *path, int mode, int *fdp, struct stat *statp, const struct server_static_mimetype **mimep)
{
    int ret = 0;
    
    // strip off the leading prefix
    if (ss->path[strlen(ss->path) - 1] == '/') {
        path += strlen(ss->path) - 1;
    } else {
        path += strlen(ss->path);
    }
	
    if (*path++ != '/') {
        log_warning("path without leading /: %s", path);
        return 400;
    }
    
    // path lookup
    const char *lookup = path;
    char name[PATH_MAX] = { 0 };
	int dirfd = -1, filefd = -1;

    if (stat(ss->root, statp)) {
        log_pwarning("stat %s", ss->root);
        ret = server_static_error();
        goto error;
    }

    if ((dirfd = open(ss->root, O_RDONLY)) < 0) {
        log_perror("open %s", ss->root);
        return server_static_error();
    }
    
    do {
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
        
        int state = START;

        if ((state = tokenize(name, sizeof(name), parsing, &lookup, START)) < 0) {
            log_error("tokenize: %s", lookup);
            return -1;
        }
        
        if (state == START || state == EMPTY || state == SELF) {
            // skip
            continue;

        } else if (state == PARENT) {
            log_warning("unsupported directory parent traversal: %s/%s", name, lookup);
            return 404;

        } else {
            log_debug("%s", name);

            // stat for meta
            if (fstatat(dirfd, name, statp, 0)) {
                log_pwarning("fstatat %d %s", dirfd, name);
                ret = server_static_error();
                goto error;
            }

            if ((statp->st_mode & S_IFMT) == S_IFDIR && *lookup) {
                // iterate into dir
                if ((filefd = openat(dirfd, name, O_RDONLY)) < 0) {
                    log_perror("openat %s", name);
                    ret = server_static_error();
                    goto error;
                }

                close(dirfd);
                dirfd = filefd;

            } else {
                // hit target file?
                if ((filefd = openat(dirfd, name, mode, 0644)) < 0) {
                    log_perror("open %s", path);
                    ret = server_static_error();
                    goto error;
                }

                break;
            }
        }
    } while (*lookup);

    if (filefd >= 0) {
        close(dirfd);
        dirfd = -1;
    } else {
        filefd = dirfd;
        dirfd = -1;
    }

    // figure out mimetype, as we have the full path here
    if ((ret = server_static_lookup_mimetype(mimep, ss, name)) < 0) {
        log_perror("server_static_lookup_mimetype: %s", name);
        goto error;
    }
    
    if (ret) {
        log_warning("no mimetype: %s", name);
        ret = 0;
        *mimep = NULL;
    }

    *fdp = filefd;

    filefd = -1;

error:
    if (filefd >= 0)
        close(filefd);

    if (dirfd >= 0)
        close(dirfd);

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

	s->handler.request = server_static_request;

    const char *method = (flags & SERVER_STATIC_PUT) ? "PUT" : "GET";

    log_info("%s %s -> %s", method, path, root);

	if (server_add_handler(server, method, path, &s->handler)) {
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
	free(s);
}
