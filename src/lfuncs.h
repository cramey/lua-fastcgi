#ifndef LFUNCS_H
#define LFUNCS_H

// Writes FCGI output followed by a carriage return
int LF_print(lua_State *);

// Writes FCGI output without a carriage return
int LF_write(lua_State *);

// loadstring() function with anti-bytecode security measures
int LF_loadstring(lua_State *);

// loadfile() function with sandboxing security measures
int LF_loadfile(lua_State *);

// dofile() function with sandboxing security measures
int LF_dofile(lua_State *);

#endif
