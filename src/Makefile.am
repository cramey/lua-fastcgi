AM_CFLAGS =
AM_CFLAGS += -std=gnu99 -Wall -O2

bin_PROGRAMS = lua-fastcgi

lua_fastcgi_LDADD =
lua_fastcgi_LDADD += -lfcgi -llua5.1 -lm -lpthread 


lua_fastcgi_SOURCES =            \
    lfconfig.c lfconfig.h        \
    lfuncs.c lfuncs.h            \
    lua.c lua.h                  \
    lua-fastcgi.c lua-fastcgi.h

