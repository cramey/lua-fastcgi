#ifndef LUA_H
#define LUA_H

#define LF_ERRNONE     0
#define LF_ERRANY      1
#define LF_ERRACCESS   2
#define LF_ERRMEMORY   3
#define LF_ERRNOTFOUND 4
#define LF_ERRSYNTAX   5
#define LF_ERRBYTECODE 6
#define LF_ERRNOPATH   7
#define LF_ERRNONAME   8

typedef struct {
	FCGX_Stream *response;
	int committed;
} LF_state;

typedef struct {
	size_t memory;
	struct timeval cpu;
	size_t output;
} LF_limits;


lua_State *LF_newstate(int, char *);
LF_limits *LF_newlimits();
void LF_setlimits(LF_limits *, size_t, size_t, uint32_t, uint32_t);
void LF_enablelimits(lua_State *, LF_limits *);
void LF_parserequest(lua_State *l, FCGX_Request *, LF_state *);
void LF_emptystack(lua_State *);
int LF_fileload(lua_State *, const char *, char *);
int LF_loadscript(lua_State *);
void LF_closestate(lua_State *);

#endif
