# vim : set noexpandtab :

CFLAGS = -g -Wall
CPPFLAGS = -Isrc -std=c99
LDFLAGS = 
LIBS = 

SRC_DIRS = $(filter %/,$(wildcard src/*/))
SRC_COMMON = $(wildcard src/common/*.c)
OBJ_COMMON = $(SRC_COMMON:%.c=build/%.o)

TEST_DIRS = $(filter %/,$(wildcard test/*/))
TEST_SRCS = $(wildcard test/*/*.c)

all: dirs

bin/main: build/src/main.o build/src/common/log.o
bin/test-url: build/test/url.o build/src/common/log.o build/src/common/url.o

dirs:
	mkdir -p bin bin/test
	mkdir -p build/src $(SRC_DIRS:%=build/%)
	mkdir -p build/test $(TEST_DIRS:%=build/%)

bin/%:
	$(CC) $(LDFLAGS) $+ -o $@ $(LIBS)

build/%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $*.c -o build/$*.o
	$(CC) -MM $(CPPFLAGS) $*.c -MT build/$*.o -MF build/$*.d

# existing .d files for rebuilding existing .o's
-include $(wildcard build/*.d)

clean:
	rm -rf core build/*/*/* bin/*

.PHONY: dirs clean
