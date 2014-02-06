# vim: set noexpandtab:

# configuration
VALGRIND =
SSL =

LOCAL_INCLUDE 	= local/include
LOCAL_LIB	= local/lib

# PCL
PCL_LIB		= pcl

# SSL
SSL_LIB     = $(SSL:%=ssl)

# ifdefs for code
CPPDEFS = $(VALGRIND:%=VALGRIND) $(SSL:%=WITH_SSL)

CFLAGS = -g -Wall
CPPFLAGS = -Isrc -std=gnu99 $(CPPDEFS:%=-D%) $(LOCAL_INCLUDE:%=-I%)
LDFLAGS = $(LOCAL_LIB:%=-L%)
LIBS = $(PCL_LIB:%=-l%) $(SSL_LIB:%=-l%)


SRC_DIRS = $(filter %/,$(wildcard src/*/))
SRC_COMMON = $(wildcard src/common/*.c)
OBJ_COMMON = $(SRC_COMMON:%.c=build/%.o)

TEST_DIRS = $(filter %/,$(wildcard test/*/))
TEST_SRCS = $(wildcard test/*/*.c)

BUILD_SSL = $(SSL:%=build/src/common/ssl.o)

all: build bin/client bin/server

test: bin/test-url bin/test-http
	bin/test-url
	bin/test-http 'HTTP/1.1 200 OK' 'Host: foo'

bin/client: build/src/client.o \
	build/src/client/client.o \
    $(BUILD_SSL) \
	build/src/common/tcp.o build/src/common/tcp_client.o \
	build/src/common/sock.o build/src/common/event.o \
	build/src/common/http.o build/src/common/stream.o \
	build/src/common/url.o build/src/common/parse.o \
	build/src/common/util.o \
	build/src/common/log.o

bin/server: build/src/server.o \
	build/src/server/server.o \
	build/src/server/static.o \
	build/src/common/tcp.o build/src/common/tcp_server.o \
	build/src/common/sock.o build/src/common/event.o \
	build/src/common/http.o build/src/common/stream.o \
	build/src/common/url.o build/src/common/parse.o \
	build/src/common/daemon.o \
	build/src/common/util.o \
	build/src/common/log.o

bin/test-url: \
	build/test/url.o \
	build/test/test.o \
	build/src/common/url.o build/src/common/parse.o build/src/common/log.o

bin/test-http: \
	build/test/http.o \
	build/test/test.o \
	build/src/common/http.o build/src/common/stream.o \
    build/src/common/parse.o build/src/common/util.o build/src/common/log.o

bin/test-parse: \
	build/test/parse.o \
	build/test/test.o \
	build/src/common/parse.o \
	build/src/common/log.o

build:
	mkdir -p bin bin/test
	mkdir -p build/src $(SRC_DIRS:%=build/%)
	mkdir -p build/test $(TEST_DIRS:%=build/%)

bin/%:
	$(CC) $(LDFLAGS) $+ -o $@ $(LIBS)

build/%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $*.c -o build/$*.o
	$(CC) -MM $(CPPFLAGS) $*.c -MT build/$*.o -MF build/$*.d

# existing .d files for rebuilding existing .o's
-include $(wildcard build/*/*.d)

clean:
	rm -rf core build/*/*/* bin/*

.PHONY: clean test
