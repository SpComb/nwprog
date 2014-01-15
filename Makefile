CFLAGS = -g -Wall
CPPFLAGS = -Isrc -std=c99
LDFLAGS = 
LIBS = 

all: build

bin/main: build/src/main.o

build:
	mkdir -p build/src 

bin/%:
	$(CC) $(LDFLAGS) $+ -o $@ $(LIBS)

build/%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $*.c -o build/$*.o
	$(CC) -MM $(CPPFLAGS) $*.c -MT build/$*.o -MF build/$*.d

# existing .d files for rebuilding existing .o's
-include $(wildcard build/*.d)

clean:
	rm -rf core build/*/*/* bin/*

.PHONY: prepare clean
