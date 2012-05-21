#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <fcgiapp.h>

#include <lua5.1/lua.h>
#include <lua5.1/lauxlib.h>

#include "lua.h"
#include "lfuncs.h"


// replacement print function, outputs to FCGI stream
static int LF_pprint(lua_State *l, int cr)
{
	int args = lua_gettop(l);

	// Fetch the response
	lua_pushstring(l, "STATE");
	lua_rawget(l, LUA_REGISTRYINDEX);
	LF_state *state = lua_touserdata(l, args+1);
	lua_pop(l, 1);

	// fetch limits
	lua_pushstring(l, "RESPONSE_LIMIT");
	lua_rawget(l, LUA_REGISTRYINDEX);
	size_t *limit = lua_touserdata(l, args+1);
	lua_pop(l, 1);

	// If the response isn't committed, send the header
	if(!state->committed){
		lua_getglobal(l, "HEADER");
		if(!lua_istable(l, args+1)){ luaL_error(l, "Invalid HEADER (Not table)."); }

		lua_pushstring(l, "Status");
		lua_rawget(l, args+1);

		// If the status has been explicitly set, send that
		if(!lua_isnil(l, args+2)){
			if(!lua_isstring(l, args+2)){
				luaL_error(l, "Invalid HEADER (Invalid Status).");
			}

			size_t len;
			const char *str = lua_tolstring(l, args+2, &len);

			if(limit){
				if((len+10) > *limit){ luaL_error(l, "Output limit exceeded."); }
				*limit -= (len+10);
			}

			FCGX_PutStr("Status: ", 8, state->response);
			FCGX_PutStr(str, len, state->response);
			FCGX_PutStr("\r\n", 2, state->response);
			state->committed = 1;
		}
		lua_pop(l, 1); // Pop the status

		// Loop over the header, ignoring status, but sending everything else
		lua_pushnil(l);
		while(lua_next(l, args+1)){
			// If the key or the value isn't a string (or number) throw an error
			if(!lua_isstring(l, args+2) || !lua_isstring(l, args+3)){
				luaL_error(l, "Invalid HEADER (Invalid key and/or value).");
			}

			size_t keylen = 0;
			const char *key = lua_tolstring(l, args+2, &keylen);
			if(keylen == 6 && memcmp(key, "Status", 6) == 0){
				// Clear the last value out
				lua_pop(l, 1);
				continue;
			}

			size_t vallen = 0;
			const char *val = lua_tolstring(l, args+3, &vallen);

			if(limit){
				if((vallen+keylen+4) > *limit){ luaL_error(l, "Output limit exceeded."); }
				*limit -= (vallen+keylen+4);
			}

			FCGX_PutStr(key, keylen, state->response);
			FCGX_PutStr(": ", 2, state->response);
			FCGX_PutStr(val, vallen, state->response);
			FCGX_PutStr("\r\n", 2, state->response);

			state->committed = 1;
			lua_pop(l, 1); // Clear the last value out
		}
		lua_pop(l, 1); // Clear the table out

		if(limit){
			if(2 >= *limit){ luaL_error(l, "Output limit exceeded."); }
			*limit -= 2;
		}

		FCGX_PutS("\r\n", state->response);
		state->committed = 1;
	}

	size_t strlen;
	const char *str;
	for(int i=1; i <= args; i++){
		switch(lua_type(l, i)){
			case LUA_TSTRING:
			case LUA_TNUMBER:
			case LUA_TBOOLEAN:
				str = lua_tolstring(l, i, &strlen);
				if(limit){
					if(strlen > *limit){ luaL_error(l, "Output limit exceeded."); }
					*limit -= strlen;
				}

				FCGX_PutStr(str, strlen, state->response);
			break;

			default: /* Ignore other types */ break;
		}
	}

	if(cr){
		if(limit){
			if(*limit == 0){ luaL_error(l, "Output limit exceeded."); }
			(*limit)--;
		}

		FCGX_PutChar('\n', state->response);
	}
	return 0;
}


int LF_print(lua_State *l){ return LF_pprint(l, 1); }
int LF_write(lua_State *l){ return LF_pprint(l, 0); }


int LF_loadstring(lua_State *l)
{
	size_t sz;
	const char *s = luaL_checklstring(l, 1, &sz);
	
	if(sz > 3 && memcmp(s, LUA_SIGNATURE, 4) == 0){
		lua_pushnil(l);
		lua_pushstring(l, "Compiled bytecode not supported.");
		return 2;
	}

	if(luaL_loadbuffer(l, s, sz, luaL_optstring(l, 2, s)) == 0){
		return 1;
	} else {
		lua_pushnil(l);
		lua_insert(l, -2);
		return 2;
	}
}


int LF_loadfile(lua_State *l)
{
	size_t sz;
	const char *spath = luaL_checklstring(l, 1, &sz);

  lua_pushstring(l, "DOCUMENT_ROOT");
	lua_rawget(l, LUA_REGISTRYINDEX);
	char *document_root = lua_touserdata(l, -1);
	lua_pop(l, 1);

	if(document_root == NULL){
		lua_pushnil(l);
		lua_pushstring(l, "DOCUMENT_ROOT not defined.");
		return 2;
	}

	size_t dz = strlen(document_root);

	if(dz == 0){
		lua_pushnil(l);
		lua_pushstring(l, "DOCUMENT_ROOT empty.");
		return 2;
	}

	size_t hz = dz + sz;
	if((hz + 2) > 4096){
		lua_pushnil(l);
		lua_pushstring(l, "Path too large.");
		return 2;
	}

	char hpath[4096];
	memcpy(&hpath[0], document_root, dz);
	if(hpath[dz-1] != '/' && spath[0] != '/'){ hpath[dz] = '/'; }
	memcpy(&hpath[dz+1], spath, sz);
	hpath[hz+1] = 0;

	char rpath[4096];
	char *ptr = realpath(hpath, rpath);
	if(ptr == NULL || memcmp(document_root, rpath, dz) != 0){
		lua_pushnil(l);
		lua_pushstring(l, "Invalid path.");
		return 2;
	}

	switch(LF_fileload(l, &spath[0], &hpath[0])){
		case 0:
			return 1;
		break;

		case LF_ERRACCESS:
			lua_pushnil(l);
			lua_pushstring(l, "Access denied.");
		break;

		case LF_ERRMEMORY:
			lua_pushnil(l);
			lua_pushstring(l, "Not enough memory.");
		break;

		case LF_ERRNOTFOUND:
			lua_pushnil(l);
			lua_pushstring(l, "No such file or directory.");
		break;

		case LF_ERRSYNTAX:
			lua_pushnil(l);
			lua_insert(l, -2);
		break;

		case LF_ERRBYTECODE:
			lua_pushnil(l);
			lua_pushstring(l, "Compiled bytecode not supported.");
		break;

		case LF_ERRNOPATH:
		case LF_ERRNONAME:
			lua_pushnil(l);
			lua_pushstring(l, "Invalid path.");
		break;
	}
	
	return 2;
}


int LF_dofile(lua_State *l)
{
	int r = LF_loadfile(l);
	if(r == 1 && lua_isfunction(l, -1)){
		lua_call(l, 0, LUA_MULTRET);
		return lua_gettop(l) - 1;
	} else {
		lua_error(l);
	}

	return 0;
}
