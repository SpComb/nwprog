CFLAGS = -g -Wall
CPPFLAGS = -Isrc -std=c99
LDFLAGS = 
LIBS = 

SRC_DIRS = $(filter %/,$(wildcard src/*/))
SRC_COMMON = $(wildcard src/common/*.c)
OBJ_COMMON = $(SRC_COMMON:%.c=build/%.o)

all: build

build:
	mkdir -p $(SRC_DIRS:%=build/%)

bin/%: build/src/%.o $(OBJ_COMMON)
	$(CC) $(LDFLAGS) $+ -o $@ $(LIBS)

build/%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $*.c -o build/$*.o
	$(CC) -MM $(CPPFLAGS) $*.c -MT build/$*.o -MF build/$*.d

# existing .d files for rebuilding existing .o's
-include $(wildcard build/*.d)

clean:
	rm -rf core build/*/*/* bin/*

.PHONY: build clean
