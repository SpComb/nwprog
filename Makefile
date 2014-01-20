# vim : set noexpandtab :

CFLAGS = -g -Wall
CPPFLAGS = -Isrc -std=gnu99
LDFLAGS = 
LIBS = -lpcl

SRC_DIRS = $(filter %/,$(wildcard src/*/))
SRC_COMMON = $(wildcard src/common/*.c)
OBJ_COMMON = $(SRC_COMMON:%.c=build/%.o)

TEST_DIRS = $(filter %/,$(wildcard test/*/))
TEST_SRCS = $(wildcard test/*/*.c)

all: build bin/client bin/server

test: bin/test-url bin/test-http
	bin/test-url
	bin/test-http 'HTTP/1.1 200 OK' 'Host: foo'

doc: doc/README.html doc/diary.html

bin/client: build/src/client.o \
	build/src/client/client.o \
	build/src/common/tcp.o build/src/common/tcp_client.o \
	build/src/common/sock.o \
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

bin/test-url: build/test/url.o build/src/common/url.o build/src/common/parse.o build/src/common/log.o
bin/test-http: build/test/http.o build/src/common/http.o build/src/common/parse.o build/src/common/util.o build/src/common/log.o

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

doc/README.html: README
	markdown $< > $@

doc/diary.txt:
	hg log -r : --style doc/diary.style > $@

doc/diary.html: doc/diary.txt
	markdown $< > $@

clean:
	rm -rf core build/*/*/* bin/*

.PHONY: clean test doc/diary.txt
