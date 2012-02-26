typedef struct {
	int committed;
	FCGX_Stream *out;
} LF_response;

typedef struct {
	size_t memory;
	struct timeval cpu;
	size_t output;
} LF_limits;


lua_State *LF_newstate(int, char *);
LF_limits *LF_newlimits();
void LF_setlimits(LF_limits *, size_t, size_t, uint32_t, uint32_t);
void LF_enablelimits(lua_State *, LF_limits *);
void LF_parserequest(lua_State *l, FCGX_Request *, LF_response *);
void LF_emptystack(lua_State *);
int LF_loadfile(lua_State *);
void LF_closestate(lua_State *);
