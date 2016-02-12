# Network Programming

This coursework project consists of several parts:

*   HTTP *client* supporting HTTP/1.1 GET and PUT methods with local files.
*   HTTP *server* supporting HTTP/1.1:
**      GET and PUT methods with local files.
**      DNS resolver queries.
*   DNS resolver *client* supporting stub resolver queries to a given resolver.
*   Limited tests for the internal components.

## Building

Building utilizes the following dependencies (Debian package names given):

* `libpcl1-dev`

Using the supplied Makefile should be enough to build the bin/client binary:

	$ make

The code has been built on the following OS/Distributions:

	Ubuntu 12.04.3 LTS                  amd64
	Debian GNU/Linux 7.0 (wheezy)       amd64

Building requires GCC 4.6 or newer, with support for -std=gnu99 and -D _POSIX_C_SOURCE=200112L

GCC 4.4 will not work due to [gcc bug #10676](http://gcc.gnu.org/bugzilla/show_bug.cgi?id=10676).

## Build options

The supplied Makefile supports some additional build configuration options for various debugging configurations.

### SSL

The client optionally supports *https://* URLs using OpenSSL.

    $ make -B SSL=1

The libssl-dev headers must be available:

* `libssl-dev`

The server does NOT provide *https* support.

### Valgrind

Due to the use of multiple stacks, running the server under valgrind will report spurious errors. This can be avoided
with the use of special valgrind-specific instrumentation to register the separate stack frames:

    $ make -B VALGRIND=1

The valgrind headers are included in the valgrind package:

* `valgrind`

## Client
	$ ./bin/client -h
    Usage: ./bin/client [options] <url> [<url>] [...]

       -h --help               Display this text
       -q --quiet              Less output
       -v --verbose            More output
       -d --debug              Debug output

       -G --get=file           GET to file
       -P --put=file           PUT from file
       -F --post=form-data     POST form data from string

       -I --iam=username       Send Iam header
          --http-11            Send HTTP/1.1 requests
       -j --parallel           Perform requests in parallel

The client will by default send an additional `Iam:` header in the request, containing the login username of the system
user running the process.

### Examples

       ./bin/client -q http://www.ietf.org/rfc/rfc2616.txt
       ./bin/client -G rfc2616.txt http://www.ietf.org/rfc/rfc2616.txt
       ./bin/client -P test.txt http://nwprog1.netlab.hut.fi:3000/test.txt
       ./bin/client -F 'name=example.com&type=A' http://localhost:8080/dns-query/

### Use of HTTP/1.1 persistent connections

       ./bin/client --http-11 http://example.com/foo /bar

### Use of parallel requests

       ./bin/client -j http://example.com/foo http://example.com/bar

Note that this is of fairly limited use pending a mechanism to provide a separate output file for each request.
The repsonse data will be arbitrarily intermixed between requests. 
No inter/intra -request ordering is guaranteed.

## Server

The server does not provide any defaults for `<listen>`, `--daemon` or `--static/upload`, and these must be
explicitly given. Running the `server` without any arguments will simply exit immediately.

    $ ./bin/server -h
    Usage: ./bin/server [options] <listen> [<listen>] [...]

       -h --help           Display this text
       -q --quiet          Less output
       -v --verbose        More output
       -d --debug          Debug output
       -L --log-file       Write log to given file

       -D --daemon         Daemonize
       -N --nfiles         Limit number of open files

       -I --iam=username   Send Iam header
       -S --static=path    Serve static files from /
       -U --upload=path    Accept PUT files to /upload
       -P --dns            Serve POST requests to /dns-query

       -R --resolver       DNS resolver address


The server will by default send an additional `Iam:` header in the response, containing the login username of the system
user running the process.

### Examples

    $ ./bin/server -v localhost:8080 -S public/
    $ ./bin/server -v localhost:8080 ip6-localhost:8080 -S public/
    $ ./bin/server -v [::]:8080 -S public/
    $ ./bin/server :1340 --static public/ --upload public/upload/ --daemon
    $ ./bin/server --static public/ --dns localhost:8081 -v

## DNS

The DNS client can be used as a simple stub resolver for testing, or to lookup large numbers of domains simultaneously.

    $ ./bin/dns -h
    Usage: ./bin/dns [options] <host> [<host>] [...]

       -h --help          Display this text
       -q --quiet         Less output
       -v --verbose       More output
       -d --debug         Debug output

       -R --resolver       DNS resolver address

### Examples:

       $ ./bin/dns example.com
            example.com has address 93.184.216.119
            example.com has IPv6 address 2606:2800:220:6d:26bf:1447:1097:aa7
       $ ./bin/dns example.com example.net
            example.com has address 93.184.216.119
            example.net has address 93.184.216.119
            example.com has IPv6 address 2606:2800:220:6d:26bf:1447:1097:aa7
            example.net has IPv6 address 2606:2800:220:6d:26bf:1447:1097:aa7

Note that the ordering of results is not specified, and may vary.

## Testing

The code includes some simple tests for some of the functionality, mostly related to string parsing:

	$ make test
