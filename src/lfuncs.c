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
	lua_pushstring(l, "RESPONSE");
	lua_rawget(l, LUA_REGISTRYINDEX);
	LF_response *response = lua_touserdata(l, args+1);
	lua_pop(l, 1);

	// fetch limits
	lua_pushstring(l, "RESPONSE_LIMIT");
	lua_rawget(l, LUA_REGISTRYINDEX);
	size_t *limit = lua_touserdata(l, args+1);
	lua_pop(l, 1);


	// If the response isn't committed, send the header
	if(!response->committed){
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

			FCGX_PutStr("Status: ", 8, response->out);
			FCGX_PutStr(str, len, response->out);
			FCGX_PutStr("\r\n", 2, response->out);
			response->committed = 1;
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

			FCGX_PutStr(key, keylen, response->out);
			FCGX_PutStr(": ", 2, response->out);
			FCGX_PutStr(val, vallen, response->out);
			FCGX_PutStr("\r\n", 2, response->out);

			response->committed = 1;
			lua_pop(l, 1); // Clear the last value out
		}
		lua_pop(l, 1); // Clear the table out

		if(limit){
			if(2 >= *limit){ luaL_error(l, "Output limit exceeded."); }
			*limit -= 2;
		}

		FCGX_PutS("\r\n", response->out);
		response->committed = 1;
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

				FCGX_PutStr(str, strlen, response->out);
			break;

			default: /* Ignore other types */ break;
		}
	}

	if(cr){
		if(limit){
			if(*limit == 0){ luaL_error(l, "Output limit exceeded."); }
			(*limit)--;
		}

		FCGX_PutChar('\n', response->out);
	}
	return 0;
}


int LF_print(lua_State *l){ return LF_pprint(l, 1); }
int LF_write(lua_State *l){ return LF_pprint(l, 0); }
