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

/*
 * Process a GET request for the given resolved file.
 */
int server_static_file_get (struct server_static *s, struct server_client *client, int fd, const struct stat *stat, const struct server_static_mimetype *mime)
{
	int err;

	// respond
	if ((err = server_response(client, 200, NULL)))
		return err;

    if (mime && (err = server_response_header(client, "Content-Type", "%s", mime->content_type)))
        return err;

    if (stat->st_size > 0) {
        if ((err = server_response_file(client, fd, stat->st_size)))
            return err;

    } else {
        // XXX: empty file
    }

	return 0;
}

int server_static_file_put (struct server_static *s, struct server_client *client, int fd, const struct server_static_mimetype *mime)
{
    int err;

    // upload
    if ((err = server_request_file(client, fd)))
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
int server_static_dir (struct server_static *s, struct server_client *client, DIR *dir, const struct url *url)
{
	struct dirent *d;
	int err;

    // ensure dir path ends in /
    if (*url->path && url->path[strlen(url->path) - 1] != '/') {
        return server_response_redirect(client, NULL, "%s/", url->path);
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
            url->path, url->path);

    if (*url->path) {
        // underneath root
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
            log_warning("server_static_lookup_mimetype: %s%s", url->path, d->d_name);
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
 * Translate request path to filesystem, returning an opened fd and stat.
 *
 * The target may be either an existing directory, an existing file, or a new file.
 *
 * `create` is the set of open() O_* flags to create the target file, or zero to open an existing file/directory.
 * Attempting to create a directory target is an error.
 */
int server_static_lookup (struct server_static *ss, const char *path, int create, int *fdp, struct stat *statp, const struct server_static_mimetype **mimep)
{
    char name[PATH_MAX] = { 0 };
	int dirfd = 0, filefd = 0; // assume not using stdin
    int ret = 0;
    
    // strip off the leading prefix for our handler
    if (!*ss->path) {
        // default path, no leading/trailing /

    } else if (ss->path[strlen(ss->path) - 1] == '/') {
        // skip path & trailing /
        path += strlen(ss->path);

    } else {
        // skip path + trailing /
        path += strlen(ss->path) + 1;
    }

    // start from our root directory
    if (stat(ss->root, statp)) {
        log_perror("stat %s", ss->root);
        ret = server_static_error();
        goto error;
    }

    if ((dirfd = open(ss->root, O_RDONLY)) < 0) {
        log_perror("open %s", ss->root);
        ret = server_static_error();
        goto error;
    }
    
    log_debug("%s %s %s", ss->root, path, create ? " (create)" : "");
    
    // parse and lookup each path component
    enum { START, EMPTY, SELF, PARENT, NAME };
    static const struct parse parsing[] = {
        { START,    '/',    EMPTY   },
        { START,    '.',    SELF,   PARSE_KEEP  },
        { START,    -1,     NAME,   PARSE_KEEP  },
        
        { SELF,     '.',    PARENT, PARSE_KEEP  },
        { SELF,     '/',    SELF    },
        { SELF,     -1,     NAME,   PARSE_KEEP  },

        { PARENT,   '/',    PARENT  },
        { PARENT,   -1,     NAME,   PARSE_KEEP  },

        { NAME,     '/',    NAME    },

        { }
    };
    const char *lookup = path;
    
    while (*lookup) {
        int state = START;

        if ((state = tokenize(name, sizeof(name), parsing, &lookup, START)) < 0) {
            log_error("tokenize: %s", lookup);
            ret = -1;
            goto error;
        }

        log_debug("%s %d %s", name, state, lookup);
        
        if (state == START || state == EMPTY || state == SELF) {
            // skip
            continue;

        } else if (state == PARENT) {
            // ..
            log_warning("unsupported directory parent traversal: %s/%s", name, lookup);
            ret = 404;
            goto error;

        } else {
            bool exists;

            // stat for meta
            if (fstatat(dirfd, name, statp, 0) == 0) {
                // success
                exists = true;

            } else if (errno == ENOENT) {
                // not found
                exists = false;

            } else {
                log_pwarning("fstatat %d %s", dirfd, name);
                ret = server_static_error();
                goto error;
            }

            if (exists && (statp->st_mode & S_IFMT) == S_IFDIR) {
                int parentfd = dirfd;

                // iterate into dir
                if ((dirfd = openat(parentfd, name, O_RDONLY)) < 0) {
                    log_perror("openat %s", name);
                    ret = server_static_error();
                    goto error;
                }
            
                log_debug("%s/", name);

                close(parentfd);

            } else {
                int mode;

                if (create && !*lookup) {
                    // hit path terminus, create/open
                    log_debug("%s*", name);
                    mode = create;

                } else if (exists) {
                    // hit target file
                    log_debug("%s?", name);
                    mode = O_RDONLY;

                } else {
                    log_warning("%s!", path);
                    return 404;
                }

                if ((filefd = openat(dirfd, name, mode, 0644)) < 0) {
                    log_perror("open %s", path);
                    ret = server_static_error();
                    goto error;
                }

                // stat after creation
                if (!exists && fstat(filefd, statp)) {
                    log_perror("fstat %d", filefd);
                    ret = server_static_error();
                    goto error;
                }

                break;
            }
        }
    }


    if (filefd) {
        // figure out mimetype from filename
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
        filefd = 0;

    } else {
        // directory
        *fdp = dirfd;
        dirfd = 0;
    }

error:
    if (filefd)
        close(filefd);

    if (dirfd)
        close(dirfd);

    return ret;
}

/*
 * Request handler.
 */
int server_static_request (struct server_handler *handler, struct server_client *client, const char *method, const struct url *url)
{
	struct server_static *ss = (struct server_static *) handler;
    const struct server_static_mimetype *mime = NULL;

	int fd = -1;
	struct stat stat;
	int ret = 0;
    int create;

	// see if there are any interesting request headers
	const char *header, *value;

	while (!(ret = server_request_header(client, &header, &value))) {

	}

	if (ret < 0)
		goto error;

    // lookup
    if (strcasecmp(method, "GET") == 0 && (ss->flags & SERVER_STATIC_GET)) {
        create = 0;

    } else if (strcasecmp(method, "PUT") == 0 && (ss->flags & SERVER_STATIC_PUT)) {
        create = O_WRONLY | O_CREAT | O_TRUNC;

    } else {
        log_warning("unknown method: %s %s", method, url->path);
        return 400;
    }

    if ((ret = server_static_lookup(ss, url->path, create, &fd, &stat, &mime))) {
        return ret;
    }

	log_info("%s %s %s %s", ss->root, method, url->path, mime ? mime->content_type : "(unknown mimetype)");
	
	// check
	if ((stat.st_mode & S_IFMT) == S_IFREG && create) {
        // put new file
        ret = server_static_file_put(ss, client, fd, mime);

    } else if ((stat.st_mode & S_IFMT) == S_IFREG) {
        // get existing file
        ret = server_static_file_get(ss, client, fd, &stat, mime);
	
	} else if ((stat.st_mode & S_IFMT) == S_IFDIR) {
		DIR *dir;
        
		if (create) {
			log_warning("cannot create directory: %s", url->path);
			return 405;
		}

		if (!(dir = fdopendir(fd))) {
			log_pwarning("fdiropen");
			ret = -1;
			goto error;
		} else {
            fd = -1;
		}
		
		ret = server_static_dir(ss, client, dir, url);

		if (closedir(dir)) {
			log_pwarning("closedir");
		}

	} else {
		log_warning("%s/%s: not a file", ss->root, url->path);
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
