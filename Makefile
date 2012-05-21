CFLAGS=-c -std=gnu99 -Wall
LDFLAGS=-O2 -Wl,-Bstatic -lfcgi -llua5.1 -Wl,-Bdynamic -lm -lpthread


.c.o:
	$(CC) $(CFLAGS) $< -o $@

all: lua-fastcgi

debug: CFLAGS+=-g -DDEBUG
debug: LDFLAGS+=-lrt
debug: lua-fastcgi

lua-fastcgi: src/lua-fastcgi.o src/lfuncs.o src/lua.o src/config.o
	$(CC) $^ $(LDFLAGS) -o $@ 

clean:
	rm -f src/*.o lua-fastcgi
